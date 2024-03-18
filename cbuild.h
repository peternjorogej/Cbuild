#pragma once

#if defined(CBUILD_WIN32)
	#if defined(_MSC_VER)
		#if !defined(_CRT_SECURE_NO_WARNINGS)
			#define _CRT_SECURE_NO_WARNINGS
		#endif // !_CRT_SECURE_NO_WARNINGS
	#endif // _MSC_VER

	#if !defined(WIN32_LEAN_AND_MEAN)
		#define WIN32_LEAN_AND_MEAN
	#endif //!WIN32_LEAN_AND_MEAN

	#if !defined(NOMINMAX)
		#define NOMINMAX
	#endif //!NOMINMAX

    #define CBUILD_SHARED_LIB_EXT "dll"
#elif defined(CBUILD_LINUX)
    #define CBUILD_SHARED_LIB_EXT "so"
#else
    #error "Unknown or unsupported platform"
    #define CBUILD_SHARED_LIB_EXT ""
#endif // CBUILD_WIN32

#define CBUILD_VERSION_MAJOR 0
#define CBUILD_VERSION_MINOR 1
#define CBUILD_VERSION_BUILD 0

#define CBUILD_HAVE_ASSERTS (1)

#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace Cbuild
{

    template<typename T>
    using List = std::vector<T>; // Type alias

    template<typename K, typename V>
    using Map = std::unordered_map<K, V>; // Type alias

    template<typename V>
    using Dictionary = std::unordered_map<std::string, V>;


    enum class BuildOutputKind : uint16_t
    {
        ConsoleApp = 0,
        StaticLibrary,
        SharedLibrary,
    };


    struct Command
    {
        std::string Name = {};
        List<std::string> Args = {};
        
        operator bool() const noexcept;
    };


    struct Configuration
    {
        std::string Name = {};
        List<std::string> Flags = {};
        List<std::string> Defines = {};
    };


    struct Project
    {
        static inline constexpr const char* const DefaultBuildConfiguration = "Debug";

        struct Workspace* Wks = nullptr;
        std::string Name = {};
        std::string Arch = {};
        std::string Language = {};
        std::string CVersion = {};
        std::string CppVersion = {};
        std::string Compiler = {};
        List<std::string> Flags = {};
        List<std::string> Defines = {};
        List<std::string> IncludeDirs = {};
        List<std::string> SourceDirs = {};
        List<std::string> LibraryDirs = {};
        List<std::string> References = {};
        List<Command> PreBuildCommands = {};
        List<Command> PostBuildCommands = {};
        Dictionary<Configuration> Configurations = {};
        BuildOutputKind OutputKind = BuildOutputKind::ConsoleApp;
        bool InferCompilerFromExtensionsOrLanguage = false; // TODO: Implement
    };


    struct Workspace
    {
        std::string Name = {};
        std::string Cwd = {};
        std::string OutputDir = {};
        std::string IntermediateDir = {}; // TODO: Implement
        List<Project> Projects = {};
        bool CheckOutputFilesBeforeBuild = false; // TODO: Implement
        bool DeleteOutputFilesIfBuildFails = false; // TODO: Implement
        bool ExecutePreBuildCommands = false; // TODO: Implement
        bool ExecutePostBuildCommands = false; // TODO: Implement

        bool Load(const char* lpXmlFilepath) noexcept;
        bool CheckOutputFiles() noexcept;
        bool DeleteOutputFiles() noexcept;
        int32_t Build(const char* lpConfiguration) const noexcept;
    };


    class IProjectBuilder
    {
    public:
        inline constexpr IProjectBuilder() noexcept = default;
        inline virtual ~IProjectBuilder() noexcept = default;
        virtual int32_t Build(const char* lpConfiguration) noexcept = 0;
        const List<Command>& GetBuildCommands() const noexcept;
        // const List<std::string>& GetOutputFiles() const noexcept; // TODO: Include?
        const Project* GetProject() const noexcept;
        BuildOutputKind GetOutputKind() const noexcept;
        static IProjectBuilder* Create(BuildOutputKind Kind, const Project* pProject) noexcept;
    
    protected:
        int32_t GenerateBuildCommandsAndOutputFiles(const char* lpConfiguration, const Command& baseCmd) noexcept;
    
    protected:
        List<Command> m_Commands = {};
        List<std::string> m_OutputFiles = {};
        const Project* m_Project = nullptr;
        BuildOutputKind m_Kind = (BuildOutputKind)(-1);
    };

}

//
// ==============================
// HOW TO BUILD
// ==============================
//
// let CC = gcc | g++
// let DEFINE = -D <def>
// let INCLUDE = -I <include>
// let LIBDIR = -L <dir>
// let REFS = -l <libname>
// let FILE = file.c | file.cpp | file.cxx | file.c++
// let OUTFILE = file.o
//
// ==============================
// A. ConsoleApp
// ==============================
// CC DEFINE... INCLUDE... <opt>... -c FILE -o outfile.o
// CC DEFINE... INCLUDE... <opt>... -c FILE -o outfile.o
// ...
// CC OUTFILE... LIBDIR... REFS... -o <proj_name.exe>
// 
// ==============================
// B. StaticLibrary
// ==============================
// CC DEFINE... INCLUDE... LIBDIR... REFS... <opt>... -c FILE -o outfile.o
// CC DEFINE... INCLUDE... LIBDIR... REFS... <opt>... -c FILE -o outfile.o
// ...
// ar -rc -o <proj_name.lib> OUTFILE...
// 
// ==============================
// C. SharedLibrary
// ==============================
// CC DEFINE... INCLUDE... LIBDIR... REFS... <opt>... -c FILE -o outfile.o
// CC DEFINE... INCLUDE... LIBDIR... REFS... <opt>... -c FILE -o outfile.o
// ...
// CC -shared -o <proj_name.dll> OUTFILE...
// 

