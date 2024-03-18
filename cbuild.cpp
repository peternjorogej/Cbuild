
#include "cbuild.h"

#include <stdio.h>

#include <type_traits>
#include <memory>
#include <filesystem>
#include <format>

#include <Windows.h>

#include <pugixml/pugixml.hpp>

#define __CBUILD_STR(x)  #x
#define CBUILD_STR(x)    __CBUILD_STR(x)

#if CBUILD_HAVE_ASSERTS
#include <stdarg.h>
#include <memory.h>
#define CBUILD_ASSERT(x, ...)  __CbuildAssert((x), __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

static void __CbuildAssert(bool bCondition, const char* lpFile, const char* lpFunction, int iLine, const char* lpFormat = nullptr, ...) noexcept
{
    if (bCondition)
    { }
    else
    {
        if (!lpFormat)
        {
            lpFormat = "Error Occurred!";
        }

        static char s_Message[1024] = {};
        memset(s_Message, 0, sizeof(s_Message));
        _snprintf_s(s_Message, sizeof(s_Message), "[%s(%d), in %s]: %s\n", lpFile, iLine, lpFunction, lpFormat);
        
        va_list vArgs;
        va_start(vArgs, lpFormat);
        vprintf_s(s_Message, vArgs);
        va_end(vArgs);

        std::terminate();
    }
}
#else
#define CBUILD_ASSERT(x, ...) ((void)((x), (__VA_ARGS__)))
#endif // CBUILD_HAVE_ASSERTS

#define CBUILD_LOG(f, ...)    fprintf((f), __VA_ARGS__)
#define CBUILD_LOG_ERROR(...) CBUILD_LOG(stderr, __VA_ARGS__)


namespace Cbuild::Argv
{

    // Build (or simple find) a better command line parser
    struct BuildOptions
    {
        const char* WksXmlFilepath = nullptr; // required
        const char* BuildConfiguration = nullptr; // required

        inline BuildOptions(int iArgc, char* ppArgv[])
        {
            static const char* const s_Usages[] =
            {
                "cbuild <file.xml> [option [--] args...]...",
            };

            static const auto ShowHelpMessage = [](const char* lpErrorMessage = nullptr, ...) -> void
            {
                if (lpErrorMessage)
                {
                    va_list vArgs;
                    va_start(vArgs, lpErrorMessage);
                    vfprintf(stderr, lpErrorMessage, vArgs);
                    va_end(vArgs);
                }
                fprintf(stderr, "\nUsage:\n");
                for (const char* const lpUsage : s_Usages)
                {
                    fprintf(stderr, "\t%s\n", lpUsage);
                }
            };

            if (iArgc < 3)
            {
                ShowHelpMessage();
                return;
            }

            // Start index offset
            constexpr size_t kOffset = 2ull; 
            // Set project xml filepath
            WksXmlFilepath = ppArgv[1];
            // Copy argv to std::vector for ease of use
            const List<std::string_view> Argv{ ppArgv + kOffset /* Ignore exe, xml */, ppArgv + iArgc };
            // Parse args
            size_t kIndex = 0ull, kArgc = Argv.size();
            while (kIndex < kArgc)
            {
                const std::string_view& arg = Argv[kIndex++];

                if (arg == "--help")
                {
                    ShowHelpMessage();
                    break;
                }
                else if (arg == "--config" && (kArgc - kIndex) >= 1ul)
                {
                    BuildConfiguration = ppArgv[kOffset + kIndex++];
                }
                else
                {
                    // Error
                    ShowHelpMessage("Arg `%s` is invalid, or has invalid argc", arg.data());
                    break;
                }
            }
        }

        inline operator bool() const noexcept
        {
            return WksXmlFilepath && BuildConfiguration;
        }
    };

}


namespace Cbuild
{
    
    enum BuildResult : int32_t
    {
        CommandProcessFailed = -69,
        WksBuildFailed = -70,
    };

}


namespace Cbuild::Builders
{

    class ConsoleAppBuilder : public IProjectBuilder
    {
    public:
        inline ConsoleAppBuilder(const Project* pProject)
        {
            m_Project = pProject;
        }

        inline virtual ~ConsoleAppBuilder() noexcept override = default;

        virtual int32_t Build(const char* lpConfiguration) noexcept override
        {
            CBUILD_ASSERT(!m_Project->Configurations.empty(), "No configurations defined!");
            CBUILD_ASSERT(lpConfiguration && *lpConfiguration, "Invalid configuration");

            const auto it = m_Project->Configurations.find({ lpConfiguration });
            if (it == m_Project->Configurations.end())
            {
                printf("[ERROR]: Configuration `%s` was not found (check if it was defined and try again)\n", lpConfiguration);
                return BuildResult::CommandProcessFailed;
            }

            const Configuration& config = it->second;
            const std::string outputFilename = std::format("{}\\{}\\{}.exe", m_Project->Wks->OutputDir, lpConfiguration, m_Project->Name);

            const auto PrepareBaseCommand = [this, &config]() -> Command
            {
                // <CC> (-D <DEF> ...) (-I <INC> ...) (-L <LIBDIR> ...) (-l <LIB> ...) (<OPTS> ...) (<IN> ...)
                Command cmd = { .Name = m_Project->Compiler };

                // Defines
                for (const auto& def : m_Project->Defines)
                {
                    cmd.Args.push_back("-D" + def);
                }
                for (const auto& def : config.Defines)
                {
                    cmd.Args.push_back("-D" + def);
                }
                // Includes
                for (const auto& inc : m_Project->IncludeDirs)
                {
                    cmd.Args.push_back("-I" + inc);
                }
                // Options
                if (m_Project->Arch.size() && m_Project->Arch == "x64")
                {
                    cmd.Args.push_back("-m64");
                }
                if (m_Project->Language == "C++")
                {
                    cmd.Args.push_back("-std=c++" + m_Project->CppVersion);
                }
                else
                {
                    cmd.Args.push_back("-std=c" + m_Project->CVersion);
                }
                for (const auto& flag : m_Project->Flags)
                {
                    cmd.Args.push_back("-" + flag);
                }
                for (const auto& flag : config.Flags)
                {
                    cmd.Args.push_back("-" + flag);
                }
                
                return cmd;
            };

            const auto PrepareFinalBuildCommand = [this, &outputFilename]() -> void
            {
                Command buildConsoleAppCmd{ .Name = m_Project->Compiler };

                // For console apps (executables), we link to the libraries when building the actual .exe file
                // Intermediate Files
                for (const auto& obj : m_OutputFiles)
                {
                    buildConsoleAppCmd.Args.push_back(obj);
                }
                // Library & References
                for (const auto& libdir : m_Project->LibraryDirs)
                {
                    buildConsoleAppCmd.Args.push_back("-L" + libdir);
                }
                for (const auto& ref : m_Project->References)
                {
                    buildConsoleAppCmd.Args.push_back("-l" + ref);
                }
                // Output
                buildConsoleAppCmd.Args.push_back("-o");
                buildConsoleAppCmd.Args.push_back(outputFilename);
                
                m_Commands.push_back(buildConsoleAppCmd);
                m_OutputFiles.push_back(outputFilename);
            };

            const auto BuildApp = [this]() -> int32_t
            {
                List<std::string> cmdLines;
                cmdLines.reserve(m_Commands.size());

                for (const auto& cmd : m_Commands)
                {
                    std::ostringstream oss;
                    oss << cmd.Name;
                    for (const auto& arg : cmd.Args)
                    {
                        oss << " " << arg;
                    }
                    cmdLines.push_back(oss.str());
                }

                BuildResult br = (BuildResult)0;
                for (const auto& cmdline : cmdLines)
                {
                    printf("%s\n", cmdline.c_str());
                    if (system(cmdline.c_str()) != 0)
                    {
                        br = BuildResult::WksBuildFailed;
                    }
                }

                return br;
            };

            GenerateBuildCommandsAndOutputFiles(lpConfiguration, PrepareBaseCommand());
            PrepareFinalBuildCommand();

            return BuildApp();
        }
    };


    class LibraryBuilder : public IProjectBuilder
    {
    public:
        inline LibraryBuilder(const Project* pProject)
        {
            m_Project = pProject;
        }

        inline virtual ~LibraryBuilder() noexcept override = default;

        virtual int32_t Build(const char* lpConfiguration) noexcept override
        {
            CBUILD_ASSERT(!m_Project->Configurations.empty(), "No configurations defined!");
            CBUILD_ASSERT(lpConfiguration && *lpConfiguration, "Invalid configuration");

            const auto it = m_Project->Configurations.find({ lpConfiguration });
            if (it == m_Project->Configurations.end())
            {
                printf("[ERROR]: Configuration `%s` was not found (check if it was defined and try again)\n", lpConfiguration);
                return BuildResult::CommandProcessFailed;
            }

            const Configuration& config = it->second;
            const char* ext = m_Project->OutputKind == BuildOutputKind::StaticLibrary ? "lib" : CBUILD_SHARED_LIB_EXT;
            const std::string outputDir = std::format("{}\\{}", m_Project->Wks->OutputDir, lpConfiguration);
            const std::string outputFilename = std::format("{}\\{}.{}", outputDir , m_Project->Name, ext);

            const auto PrepareBaseCommand = [this, &config]() -> Command
            {
                // <CC> (-D <DEF> ...) (-I <INC> ...) (-L <LIBDIR> ...) (-l <LIB> ...) (<OPTS> ...) (<IN> ...)
                Command cmd = { .Name = m_Project->Compiler };

                // Defines
                for (const auto& def : m_Project->Defines)
                {
                    cmd.Args.push_back("-D" + def);
                }
                for (const auto& def : config.Defines)
                {
                    cmd.Args.push_back("-D" + def);
                }
                // Includes
                for (const auto& inc : m_Project->IncludeDirs)
                {
                    cmd.Args.push_back("-I" + inc);
                }
                // Library & References
                for (const auto& libdir : m_Project->LibraryDirs)
                {
                    cmd.Args.push_back("-L" + libdir);
                }
                for (const auto& ref : m_Project->References)
                {
                    cmd.Args.push_back("-l" + ref);
                }
                // Options
                if (m_Project->Arch.size() && m_Project->Arch == "x64")
                {
                    cmd.Args.push_back("-m64");
                }
                if (m_Project->Language == "C++")
                {
                    cmd.Args.push_back("-std=c++" + m_Project->CppVersion);
                }
                else
                {
                    cmd.Args.push_back("-std=c" + m_Project->CVersion);
                }
                for (const auto& flag : m_Project->Flags)
                {
                    cmd.Args.push_back("-" + flag);
                }
                for (const auto& flag : config.Flags)
                {
                    cmd.Args.push_back("-" + flag);
                }
                
                return cmd;
            };

            const auto PrepareFinalBuildCommand = [this, &outputDir, &outputFilename]() -> void
            {
                Command buildLibraryCmd = {};
                if (m_Project->OutputKind == BuildOutputKind::StaticLibrary)
                {
                    buildLibraryCmd.Name = "ar";
                    buildLibraryCmd.Args.push_back("-rcs");
                }
                else
                {
                    buildLibraryCmd.Name = m_Project->Compiler;
                    buildLibraryCmd.Args.push_back("-shared");
                    buildLibraryCmd.Args.push_back(std::format("-Xlinker --out-implib {}\\{}.lib", outputDir, m_Project->Name));
                }

                // Output
                buildLibraryCmd.Args.push_back("-o");
                buildLibraryCmd.Args.push_back(outputFilename);
                // Intermediate Files
                for (const auto& obj : m_OutputFiles)
                {
                    buildLibraryCmd.Args.push_back(obj);
                }
                
                m_Commands.push_back(buildLibraryCmd);
                m_OutputFiles.push_back(outputFilename);
            };

            const auto BuildApp = [this]() -> int32_t
            {
                List<std::string> cmdLines;
                cmdLines.reserve(m_Commands.size());

                for (const auto& cmd : m_Commands)
                {
                    std::ostringstream oss;
                    oss << cmd.Name;
                    for (const auto& arg : cmd.Args)
                    {
                        oss << " " << arg;
                    }
                    cmdLines.push_back(oss.str());
                }

                BuildResult br = (BuildResult)0;
                for (const auto& cmdline : cmdLines)
                {
                    printf("%s\n", cmdline.c_str());
                    if (system(cmdline.c_str()) != 0)
                    {
                        br = BuildResult::WksBuildFailed;
                    }
                }

                return br;
            };

            GenerateBuildCommandsAndOutputFiles(lpConfiguration, PrepareBaseCommand());
            PrepareFinalBuildCommand();

            return BuildApp();
        }
    };

}


namespace Cbuild
{

    static inline constexpr const char* GetVersion() noexcept
    {
        // X.X.X
        return CBUILD_STR(CBUILD_VERSION_MAJOR) "."
               CBUILD_STR(CBUILD_VERSION_MINOR) "."
               CBUILD_STR(CBUILD_VERSION_BUILD);
    }


    class Converter
    {
    public:
        static const char* OutputKindToString(BuildOutputKind Kind) noexcept
        {
            switch (Kind)
            {
                case BuildOutputKind::ConsoleApp:    return "ConsoleApp";
                case BuildOutputKind::StaticLibrary: return "StaticLibrary";
                case BuildOutputKind::SharedLibrary: return "SharedLibrary";
                default: return CBUILD_ASSERT(false, 0), nullptr;
            }
        }

        static BuildOutputKind StringToOutputKind(const std::string_view& Value) noexcept
        {
            if (Value == "ConsoleApp")    return BuildOutputKind::ConsoleApp;
            if (Value == "StaticLibrary") return BuildOutputKind::StaticLibrary;
            if (Value == "SharedLibrary") return BuildOutputKind::SharedLibrary;
            
            return static_cast<BuildOutputKind>(-1);
        }
    };


    class XmlReadHelper
    {
    public:
        static bool LoadWorkspace(Workspace* const pWks, const char* lpXmlFilepath) noexcept
        {
            CBUILD_ASSERT(lpXmlFilepath, "Invalid filepath");

            pugi::xml_document doc;
            pugi::xml_parse_result res = doc.load_file(lpXmlFilepath, pugi::parse_full);
            CBUILD_ASSERT((bool)res, "Error in parsing `%s`. Description=%s, FileOffset=%I64d", lpXmlFilepath, res.description(), res.offset);

            const pugi::xml_node xWks = doc.first_child();

            // Attributes
            if (const auto xAttr = xWks.attribute("Name"))
            {
                pWks->Name = std::string{ xAttr.as_string() };
            }
            else
            {
                CBUILD_LOG_ERROR("Error: Workspace must have a name (offset: %lld)\n", xWks.offset_debug());
                return false;
            }
            
            // Output Directory
            if (const auto xOutputDir = xWks.child("OutputDir"))
            {
                pWks->OutputDir = std::string{ xOutputDir.child_value() };
            }
            else
            {
                pWks->OutputDir = std::string{ "bin" };
            }
            
            // Intermediates Directory
            if (const auto xIntermediateDir = xWks.child("IntermediateDir"))
            {
                pWks->IntermediateDir = std::string{ xIntermediateDir.child_value() };
            }
            else
            {
                pWks->IntermediateDir = std::string{ "bin-int" };
            }
            
            // CWD
            if (const auto xWorkingDirectory = xWks.child("WorkingDirectory"))
            {
                pWks->Cwd = std::string{ xWorkingDirectory.child_value() };
            }
            else
            {
                pWks->Cwd = std::string{ "./" };
            }

            // Projects
            for (const auto& xProject : xWks.children("Project"))
            {
                Project p = { .Wks = pWks };
                if (!LoadProject(&p, xProject))
                {
                    return false;
                }
                
                pWks->Projects.push_back(std::move(p));
            }

            return true;
        }

    public:
        static bool LoadProject(Project* const pProject, const pugi::xml_node& xProject) noexcept
        {
            // Attributes (Name, Kind, Arch, Language, (C/Cpp)Version, Compiler, ...)
            if (!ReadProjectAttributes(pProject, xProject))
            {
                return false;
            }

            // Configurations
            for (const auto& xConfiguration : xProject.children("Configuration"))
            {
                if (const auto xName = xConfiguration.attribute("Name"))
                {
                    const Configuration config = { .Name = xName.as_string() };
                    pProject->Configurations.insert({ config.Name, config });
                }
                else
                {
                    CBUILD_LOG_ERROR("Error: Configuration has no name (offset: %lld)\n", xConfiguration.offset_debug());
                    return false;
                }
            }

            // Flags (options)
            if (const auto xFlags = xProject.child("Flags"))
            {
                for (const auto& xItem : xFlags.children("Item"))
                {
                    if (const auto xConfig = xItem.attribute("Configuration"))
                    {
                        const std::string configName = std::string{ xConfig.as_string() };
                        if (const auto it = pProject->Configurations.find(configName); it != pProject->Configurations.end())
                        {
                            Configuration& config = it->second;
                            config.Flags.push_back(std::string{ xItem.child_value() });
                        }
                        else
                        {
                            CBUILD_LOG_ERROR("Error: Configuration `%s` not found\n", configName.c_str());
                            return false;
                        }
                    }
                    else
                    {
                        pProject->Flags.push_back(std::string{ xItem.child_value() });
                    }
                }
            }

            // Defines
            if (const auto xDefine = xProject.child("Defines"))
            {
                for (const auto& xItem : xDefine.children("Item"))
                {
                    if (const auto xConfig = xItem.attribute("Configuration"))
                    {
                        const std::string configName = std::string{ xConfig.as_string() };
                        if (const auto it = pProject->Configurations.find(configName); it != pProject->Configurations.end())
                        {
                            Configuration& config = it->second;
                            config.Defines.push_back(std::string{ xItem.child_value() });
                        }
                        else
                        {
                            CBUILD_LOG_ERROR("Error: Configuration `%s` not found\n", configName.c_str());
                            return false;
                        }
                    }
                    else
                    {
                        pProject->Defines.push_back(std::string{ xItem.child_value() });
                    }
                }
            }

            // Include, Source Directories
            if (const auto xIncludeDirs = xProject.child("IncludeDirs"))
            {
                for (const auto& xItem : xIncludeDirs.children("Item"))
                {
                    pProject->IncludeDirs.push_back(std::string{ xItem.child_value() });
                }
            }
            
            if (const auto xSourceDirs = xProject.child("SourceDirs"))
            {
                for (const auto& xItem : xSourceDirs.children("Item"))
                {
                    pProject->SourceDirs.push_back(std::string{ xItem.child_value() });
                }
            }

            // Library Directories & References (to libraries)
            if (const auto xLibraryDirs = xProject.child("LibraryDirs"))
            {
                for (const auto& xItem : xLibraryDirs.children("Item"))
                {
                    pProject->LibraryDirs.push_back(std::string{ xItem.child_value() });
                }
            }
            
            if (const auto xReferences = xProject.child("References"))
            {
                for (const auto& xItem : xReferences.children("Item"))
                {
                    pProject->References.push_back(std::string{ xItem.child_value() });
                }
            }

            return true;
        }
    
        static bool ReadProjectAttributes(Project* const pProject, const pugi::xml_node& xProject) noexcept
        {
            bool bIsLanguageSet = false;
            bool bIsLangVersionSet = false;
            bool bIsCompilerSet = false;

            if (const auto xAttr = xProject.attribute("Name")) { pProject->Name = std::string{ xAttr.as_string() }; }
            if (const auto xAttr = xProject.attribute("Arch")) { pProject->Arch = std::string{ xAttr.as_string() }; }
            if (const auto xAttr = xProject.attribute("Kind")) { pProject->OutputKind = Converter::StringToOutputKind(xAttr.as_string()); }
            if (const auto xAttr = xProject.attribute("Language"))   { pProject->Language = std::string{ xAttr.as_string() }; bIsLanguageSet = true; }
            if (const auto xAttr = xProject.attribute("CVersion"))   { pProject->CVersion = std::string{ xAttr.as_string() }; bIsLangVersionSet = true; }
            if (const auto xAttr = xProject.attribute("CppVersion")) { pProject->CppVersion = std::string{ xAttr.as_string() }; bIsLangVersionSet = true; }
            if (const auto xAttr = xProject.attribute("Compiler"))   { pProject->Compiler = std::string{ xAttr.as_string() }; bIsCompilerSet = true; }

            if (pProject->Name.empty())
            {
                CBUILD_LOG_ERROR("Error: Project must have a name (offset: %lld)\n", xProject.offset_debug());
                return false;
            }

            if (!bIsLanguageSet && !bIsLangVersionSet && !bIsCompilerSet)
            {
                CBUILD_LOG_ERROR("Error: At least one of 'Language', 'CVersion|CppVersion', 'Compiler' must be set (offset: %lld)\n", xProject.offset_debug());
                return false;
            }

            if (!bIsCompilerSet)
            {
                pProject->Compiler = "g++";
            }
            
            if (!bIsLanguageSet)
            {
                pProject->Language = pProject->Compiler == "gcc" ? "C" : "C++";
            }

            if (!bIsLangVersionSet)
            {
                pProject->CVersion = "89"; // C89
                pProject->CppVersion = "14"; // C++14
            }

            return true;
        }

        /* static bool ReadProjectConfiguration(Project* const pProject, const pugi::xml_node& xConfiguration) noexcept
        {
            Configuration config = {};
            // Name
            if (const auto xAttr = xConfiguration.attribute("Name"))
            {
                config.Name = std::string{ xAttr.as_string() };
            }
            else
            {
                return false;
            }

            // Flags (options)
            if (const auto xFlags = xConfiguration.child("Flags"))
            {
                for (const auto& xItem : xFlags.children("Item"))
                {
                    config.Flags.push_back(std::string{ xItem.child_value() });
                }
            }

            // Configuration-dependent Defines (e.g DEBUG, NDEBUG, RELEASE, ASSERT, ...)
            if (const auto xDefines = xConfiguration.child("Defines"))
            {
                for (const auto& xItem : xDefines.children("Item"))
                {
                    config.Defines.push_back(std::string{ xItem.child_value() });
                }
            }

            // pProject->Configurations[config.Name] = config;
            pProject->Configurations.insert({ config.Name, config });
            return true;
        } */
    };


    Command::operator bool() const noexcept
    {
        return Name.length() > 0;
    }

    
    bool Workspace::Load(const char* lpXmlFilepath) noexcept
    {
        return XmlReadHelper::LoadWorkspace(this, lpXmlFilepath);
    }
    
    bool Workspace::CheckOutputFiles() noexcept
    {
        CBUILD_ASSERT(false, "Unimplemented");
        return false;
    }

    bool Workspace::DeleteOutputFiles() noexcept
    {
        CBUILD_ASSERT(false, "Unimplemented");
        return false;
    }

    int32_t Workspace::Build(const char* lpConfiguration) const noexcept
    {
        int32_t result = EXIT_SUCCESS;
        for (const auto p : Projects)
        {
            IProjectBuilder* pBuilder = IProjectBuilder::Create(p.OutputKind, &p);
            printf("=========== Building `%s` ===========\n", p.Name.c_str());
            CBUILD_ASSERT(pBuilder != nullptr, "failed to allocate memory");
            result = pBuilder->Build(lpConfiguration);
            printf("\n");
        }

        return result;
    }

    
    const List<Command>& IProjectBuilder::GetBuildCommands() const noexcept
    {
        return m_Commands;
    }

    // const List<std::string>& IProjectBuilder::GetOutputFiles() const noexcept
    // {
    //     return m_OutputFiles;
    // }
    
    const Project* IProjectBuilder::GetProject() const noexcept
    {
        return m_Project;
    }

    BuildOutputKind IProjectBuilder::GetOutputKind() const noexcept
    {
        return m_Kind;
    }

    IProjectBuilder* IProjectBuilder::Create(BuildOutputKind Kind, const Project* pProject) noexcept
    {
        CBUILD_ASSERT(pProject != nullptr, "Invalid project");
        switch (Kind)
        {
            case BuildOutputKind::ConsoleApp:
                return new Builders::ConsoleAppBuilder{ pProject };
            case BuildOutputKind::StaticLibrary:
                return new Builders::LibraryBuilder{ pProject };
            case BuildOutputKind::SharedLibrary:
                return new Builders::LibraryBuilder{ pProject };
            default:
                CBUILD_ASSERT(false, "Invalid build output kind");
                return nullptr;
        }
    }

    int32_t IProjectBuilder::GenerateBuildCommandsAndOutputFiles(const char* lpConfiguration, const Command& baseCmd) noexcept
    {
        namespace stdfs = std::filesystem;

        const stdfs::path cwd = stdfs::path(m_Project->Wks->Cwd);
        const std::string ConfigName{ lpConfiguration }; 
        const std::string IntermediateDir = std::format("{}\\{}", m_Project->Wks->IntermediateDir, ConfigName);
        const std::string OutputDir = std::format("{}\\{}", m_Project->Wks->OutputDir, ConfigName);

        for (const auto& srcdir : m_Project->SourceDirs)
        {
            const bool bSrcDirIsCwd = srcdir == "." || srcdir == "./";
            const stdfs::path dir = bSrcDirIsCwd ? cwd : (cwd / srcdir);

            for (const stdfs::directory_entry& entry : stdfs::directory_iterator(dir))
            {
                const stdfs::path& path = entry.path();
                const std::string ext = path.extension().string();

                if (stdfs::is_regular_file(path) && (ext == ".c" || ext == ".cpp"))
                {
                    Command cmd{ baseCmd };

                    const std::string PathStr = path.string();
                    const std::string IntermediateFile = std::format("{}\\{}.o", IntermediateDir, path.stem().string());

                    cmd.Args.push_back("-c");
                    cmd.Args.push_back(PathStr);
                    cmd.Args.push_back("-o");
                    cmd.Args.push_back(IntermediateFile);

                    m_Commands.push_back(std::move(cmd));
                    m_OutputFiles.push_back(std::move(IntermediateFile));
                }
            }
        }

        return 0;
    }

}

int main(int iArgc, char* ppArgv[])
{
    const char* const* ppOldArgv = ppArgv;

    if (const Cbuild::Argv::BuildOptions bo{ iArgc, ppArgv })
    {
        Cbuild::Workspace wks = {};
        if (!wks.Load(bo.WksXmlFilepath))
        {
            return -2;
        }

        int32_t iResult = wks.Build(bo.BuildConfiguration);
        if (iResult == Cbuild::BuildResult::CommandProcessFailed)
        {
            printf("Error: Cbuild::BuildResult::CommandProcessFailed (Please check that the project file is well defined).\n");
            return -3;
        }
        if (iResult == Cbuild::BuildResult::WksBuildFailed)
        {
            printf("Error: Cbuild::BuildResult::WksBuildFailed (Build failed, fix errors and try again).\n");
            return -4;
        }

        return 0;
    }

    return -1;
}

