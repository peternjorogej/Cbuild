
#include "cbuild.h"

#include <stdio.h>

#include <type_traits>
#include <memory>
#include <filesystem>
#include <format>
#include <regex>

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
        CommandProcessingFailed = -69,
        WksBuildFailed = -70,
    };

}


namespace Cbuild::Builders
{

    struct SetResult
    {
        std::string Result = {};
        bool Succeeded = false;
    };

    // TODO: Improve
    static SetResult SetVariables(const std::string& Path, const std::string& Variable, const char* lpValue) noexcept
    {
        static const Dictionary<bool> s_ValidVariables =
        {
            { "Configuration", true }
        };

        if (s_ValidVariables.find(Variable) == s_ValidVariables.end())
        {
            return { .Result = {}, .Succeeded = false };
        }

        // If `Path` doesn't have a variable, exit early
        // Not all paths have variables
        if (Path.find('$', 0ull) == std::string::npos)
        {
            return { .Result = Path, .Succeeded = true };
        }

        // Pattern: $(Variable)
        const std::regex regex{ R"(\$\([a-zA-Z0-9_]*\))" };
        try
        {
            if (std::regex_search(Path, regex))
            {
                return { .Result = std::regex_replace(Path, regex, lpValue), .Succeeded = true };
            }
        }
        // Let all the catches fall through
        catch(const std::regex_error& e) { }
        catch(const std::exception& e) { }
        catch(...) { }

        return { .Result = {}, .Succeeded = false };
    }

    static SetResult SetVariables(const std::filesystem::path& Path, const std::string& Variable, const char* lpValue) noexcept
    {
        const std::string path = Path.string();
        return SetVariables(path, Variable, lpValue);
    }


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
            if (!VerifyConfiguration(lpConfiguration))
            {
                return BuildResult::CommandProcessingFailed;
            }

            const auto it = m_Project->Configurations.find({ lpConfiguration });
            if (it == m_Project->Configurations.end())
            {
                CBUILD_LOG_ERROR("[ERROR]: Configuration `%s` was not found (check if it was defined and try again)\n", lpConfiguration);
                return BuildResult::CommandProcessingFailed;
            }

            const Configuration& config = it->second;
            const std::string outputFilename = std::format("{}\\{}\\{}.exe", m_Project->Wks->OutputDir, lpConfiguration, m_Project->Name);

            Command baseCmd = { .Name = m_Project->Compiler };
            PrepareBaseCommand(&baseCmd, config, lpConfiguration);

            GenerateBuildCommandsAndOutputFiles(lpConfiguration, baseCmd);
            PrepareFinalBuildCommand({}, outputFilename, lpConfiguration);

            return RunBuildCommands();
        }

        virtual void PrepareFinalBuildCommand(const std::string& OutputDir, const std::string& OutputFilename, const char* lpConfiguration) noexcept override
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
                const auto[dir, succeeded] = SetVariables(libdir, "Configuration", lpConfiguration);
                CBUILD_ASSERT(succeeded, "Failed to set library directory (`%s`) from variable", libdir.c_str());
                buildConsoleAppCmd.Args.push_back("-L" + dir);
            }
            for (const auto& ref : m_Project->References)
            {
                buildConsoleAppCmd.Args.push_back("-l" + ref);
            }
            // Output
            buildConsoleAppCmd.Args.push_back("-o");
            buildConsoleAppCmd.Args.push_back(OutputFilename);
            
            m_Commands.push_back(buildConsoleAppCmd);
            m_OutputFiles.push_back(OutputFilename);
        }
    };


    class StaticLibraryBuilder : public IProjectBuilder
    {
    public:
        inline StaticLibraryBuilder(const Project* pProject)
        {
            m_Project = pProject;
        }

        inline virtual ~StaticLibraryBuilder() noexcept override = default;

        virtual int32_t Build(const char* lpConfiguration) noexcept override
        {
            if (!VerifyConfiguration(lpConfiguration))
            {
                return BuildResult::CommandProcessingFailed;
            }

            const auto it = m_Project->Configurations.find({ lpConfiguration });
            if (it == m_Project->Configurations.end())
            {
                printf("[ERROR]: Configuration `%s` was not found (check if it was defined and try again)\n", lpConfiguration);
                return BuildResult::CommandProcessingFailed;
            }

            const Configuration& config = it->second;
            const char* ext = m_Project->OutputKind == BuildOutputKind::StaticLib ? "lib" : CBUILD_SHARED_LIB_EXT;
            const std::string outputDir = std::format("{}\\{}", m_Project->Wks->OutputDir, lpConfiguration);
            const std::string outputFilename = std::format("{}\\{}.{}", outputDir , m_Project->Name, ext);

            Command baseCmd = { .Name = m_Project->Compiler };
            PrepareBaseCommand(&baseCmd, config, lpConfiguration);

            GenerateBuildCommandsAndOutputFiles(lpConfiguration, baseCmd);
            PrepareFinalBuildCommand(outputDir, outputFilename, lpConfiguration);

            return RunBuildCommands();
        }

        virtual void PrepareFinalBuildCommand(const std::string& OutputDir, const std::string& OutputFilename, const char* lpConfiguration) noexcept override
        {
            Command buildLibraryCmd = {};

            buildLibraryCmd.Name = "ar";
            buildLibraryCmd.Args.push_back("-rcs");

            // Output
            buildLibraryCmd.Args.push_back("-o");
            buildLibraryCmd.Args.push_back(OutputFilename);
            // Library & References
            for (const auto& libdir : m_Project->LibraryDirs)
            {
                const auto[dir, succeeded] = SetVariables(libdir, "Configuration", lpConfiguration);
                CBUILD_ASSERT(succeeded, "Failed to set library directory (`%s`) from variable", libdir.c_str());
                buildLibraryCmd.Args.push_back("-L" + dir);
            }
            for (const auto& ref : m_Project->References)
            {
                buildLibraryCmd.Args.push_back("-l" + ref);
            }
            // Intermediate Files
            for (const auto& obj : m_OutputFiles)
            {
                buildLibraryCmd.Args.push_back(obj);
            }
            
            m_Commands.push_back(buildLibraryCmd);
            m_OutputFiles.push_back(OutputFilename);
        }
    };


    class SharedLibraryBuilder : public IProjectBuilder
    {
    public:
        inline SharedLibraryBuilder(const Project* pProject)
        {
            m_Project = pProject;
        }

        inline virtual ~SharedLibraryBuilder() noexcept override = default;

        virtual int32_t Build(const char* lpConfiguration) noexcept override
        {
            if (!VerifyConfiguration(lpConfiguration))
            {
                return BuildResult::CommandProcessingFailed;
            }

            const auto it = m_Project->Configurations.find({ lpConfiguration });
            if (it == m_Project->Configurations.end())
            {
                printf("[ERROR]: Configuration `%s` was not found (check if it was defined and try again)\n", lpConfiguration);
                return BuildResult::CommandProcessingFailed;
            }

            const Configuration& config = it->second;
            const char* ext = m_Project->OutputKind == BuildOutputKind::StaticLib ? "lib" : CBUILD_SHARED_LIB_EXT;
            const std::string outputDir = std::format("{}\\{}", m_Project->Wks->OutputDir, lpConfiguration);
            const std::string outputFilename = std::format("{}\\{}.{}", outputDir , m_Project->Name, ext);

            Command baseCmd = { .Name = m_Project->Compiler };
            PrepareBaseCommand(&baseCmd, config, lpConfiguration);

            GenerateBuildCommandsAndOutputFiles(lpConfiguration, baseCmd);
            PrepareFinalBuildCommand(outputDir, outputFilename, lpConfiguration);

            return RunBuildCommands();
        }
    
        virtual void PrepareFinalBuildCommand(const std::string& OutputDir, const std::string& OutputFilename, const char* lpConfiguration) noexcept override
        {
            Command buildLibraryCmd = {};

            buildLibraryCmd.Name = m_Project->Compiler;
            buildLibraryCmd.Args.push_back("-shared");
            buildLibraryCmd.Args.push_back(std::format("-Xlinker --out-implib {}\\{}.lib", OutputDir, m_Project->Name));

            // Library & References
            for (const auto& libdir : m_Project->LibraryDirs)
            {
                const auto[dir, succeeded] = SetVariables(libdir, "Configuration", lpConfiguration);
                CBUILD_ASSERT(succeeded, "Failed to set library directory (`%s`) from variable", libdir.c_str());
                buildLibraryCmd.Args.push_back("-L" + dir);
            }
            for (const auto& ref : m_Project->References)
            {
                buildLibraryCmd.Args.push_back("-l" + ref);
            }
            // Output
            buildLibraryCmd.Args.push_back("-o");
            buildLibraryCmd.Args.push_back(OutputFilename);
            // Intermediate Files
            for (const auto& obj : m_OutputFiles)
            {
                buildLibraryCmd.Args.push_back(obj);
            }
            
            m_Commands.push_back(buildLibraryCmd);
            m_OutputFiles.push_back(OutputFilename);
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
        static inline const char* OutputKindToString(BuildOutputKind Kind) noexcept
        {
            switch (Kind)
            {
                case BuildOutputKind::ConsoleApp: return "ConsoleApp";
                case BuildOutputKind::StaticLib:  return "StaticLib";
                case BuildOutputKind::SharedLib:  return "SharedLib";
                default: return CBUILD_ASSERT(false, 0), nullptr;
            }
        }

        static inline BuildOutputKind StringToOutputKind(const std::string_view& Value) noexcept
        {
            if (Value == "ConsoleApp") return BuildOutputKind::ConsoleApp;
            if (Value == "StaticLib")  return BuildOutputKind::StaticLib;
            if (Value == "SharedLib")  return BuildOutputKind::SharedLib;
            
            return static_cast<BuildOutputKind>(-1);
        }
    };


    class XmlReadHelper
    {
    public:
        static bool LoadWorkspace(Workspace* const pWks, const char* lpXmlFilepath) noexcept
        {
            if (!lpXmlFilepath)
            {
                CBUILD_LOG_ERROR("Invalid project filepath");
                return false;
            }

            pugi::xml_document doc;
            pugi::xml_parse_result res = doc.load_file(lpXmlFilepath, pugi::parse_full);
            if (!(bool)res)
            {
                constexpr const char* const lpErrorMessage = "Error in parsing `%s`. Description=%s, FileOffset=%I64d";
                CBUILD_LOG_ERROR(lpErrorMessage, lpXmlFilepath, res.description(), res.offset);
                return false;
            }

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
                // pWks->OutputDir = std::string{ "bin" };
                pWks->OutputDir = std::string{ "./" };
            }
            
            // Intermediates Directory
            if (const auto xIntermediateDir = xWks.child("IntermediateDir"))
            {
                pWks->IntermediateDir = std::string{ xIntermediateDir.child_value() };
            }
            else
            {
                // pWks->IntermediateDir = std::string{ "bin-int" };
                pWks->IntermediateDir = std::string{ "./" };
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
            case BuildOutputKind::StaticLib:
                return new Builders::StaticLibraryBuilder{ pProject };
            case BuildOutputKind::SharedLib:
                return new Builders::SharedLibraryBuilder{ pProject };
            default:
                CBUILD_ASSERT(false, "Invalid build output kind");
                return nullptr;
        }
    }

    void IProjectBuilder::PrepareBaseCommand(Command* const pCmd, const Configuration& config, const char* lpConfiguration) noexcept
    {
        // Defines
        for (const auto& def : m_Project->Defines)
        {
            pCmd->Args.push_back("-D" + def);
        }
        for (const auto& def : config.Defines)
        {
            pCmd->Args.push_back("-D" + def);
        }
        // Includes
        for (const auto& inc : m_Project->IncludeDirs)
        {
            pCmd->Args.push_back("-I" + inc);
        }
        // Options
        if (m_Project->Arch.size() && m_Project->Arch == "x64")
        {
            pCmd->Args.push_back("-m64");
        }
        if (m_Project->Language == "C++")
        {
            pCmd->Args.push_back("-std=c++" + m_Project->CppVersion);
        }
        else
        {
            pCmd->Args.push_back("-std=c" + m_Project->CVersion);
        }
        for (const auto& flag : m_Project->Flags)
        {
            pCmd->Args.push_back(flag);
        }
        for (const auto& flag : config.Flags)
        {
            pCmd->Args.push_back(flag);
        }
    }

    int32_t IProjectBuilder::RunBuildCommands() noexcept
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
    }

    bool IProjectBuilder::VerifyConfiguration(const char* lpConfiguration) noexcept
    {
        if (m_Project->Configurations.empty())
        {
            CBUILD_LOG_ERROR("[ERROR]: No configuration was defined\n");
            return false;
        }
        if (!lpConfiguration || !(*lpConfiguration))
        {
            CBUILD_LOG_ERROR("[ERROR]: Invalid configuration provided (`%s`)\n", lpConfiguration);
            return false;
        }

        return true;
    }

    int32_t IProjectBuilder::GenerateBuildCommandsAndOutputFiles(const char *lpConfiguration, const Command &baseCmd) noexcept
    {
        namespace stdfs = std::filesystem;

        static constexpr uint32_t DirectoryWalkRecursionDepth = 8ul;

        // Data needed to traverse the directory (refs used since there's no need for copies)
        // NOTE: Too much?
        struct DirWalkInfo
        {
            IProjectBuilder*   Builder;
            const Command&     BaseCmd;
            const std::string& ObjectDir;
            const stdfs::path& Directory;
            uint32_t           Depth;
        };

        // Lambdas have to be defined with a type to be used recusively
        using pfnWalkDirectory = void(*)(const DirWalkInfo&);

        static const pfnWalkDirectory WalkDirectory = [](const DirWalkInfo& dwi) -> void
        {
            for (const stdfs::directory_entry& entry : stdfs::directory_iterator(dwi.Directory))
            {
                const stdfs::path& path = entry.path();
                const std::string ext = path.extension().string();

                if (stdfs::is_regular_file(path) && (ext == ".c" || ext == ".cpp"))
                {
                    Command cmd{ dwi.BaseCmd };

                    const std::string PathStr = path.string();
                    const std::string IntermediateFile = std::format("{}\\{}.o", dwi.ObjectDir, path.stem().string());

                    cmd.Args.push_back("-c");
                    cmd.Args.push_back(PathStr);
                    cmd.Args.push_back("-o");
                    cmd.Args.push_back(IntermediateFile);

                    dwi.Builder->m_Commands.push_back(std::move(cmd));
                    dwi.Builder->m_OutputFiles.push_back(std::move(IntermediateFile));
                }

                // Make sure we don't exceed the recursion limit (8 sub-directories deep should be enough)
                if (stdfs::is_directory(path) && dwi.Depth <= DirectoryWalkRecursionDepth)
                {
                    // Recursively traverse the source directory
                    const DirWalkInfo info
                    {
                        .Builder = dwi.Builder,
                        .BaseCmd = dwi.BaseCmd,
                        .ObjectDir = dwi.ObjectDir,
                        .Directory = path,
                        .Depth = dwi.Depth + 1ul
                    };
                    WalkDirectory(info);
                }
            }
        };

        // Get the source files from the source directories
        const stdfs::path Cwd = stdfs::path(m_Project->Wks->Cwd);
        const std::string ConfigName{ lpConfiguration }; 
        const std::string IntermediateDir = std::format("{}\\{}", m_Project->Wks->IntermediateDir, ConfigName);
        const std::string OutputDir = std::format("{}\\{}", m_Project->Wks->OutputDir, ConfigName);

        for (const auto& srcdir : m_Project->SourceDirs)
        {
            const bool bSrcDirIsCwd = srcdir == "." || srcdir == "./" || srcdir == ".\\";
            const stdfs::path dir = bSrcDirIsCwd ? Cwd : (Cwd / srcdir);

            const DirWalkInfo dwi{ .Builder = this, .BaseCmd = baseCmd, .ObjectDir = IntermediateDir, .Directory = dir, .Depth = 0 };
            WalkDirectory(dwi);
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
        if (iResult == Cbuild::BuildResult::CommandProcessingFailed)
        {
            printf("Error: Cbuild::BuildResult::CommandProcessingFailed (Please check that the project file is well defined).\n");
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

