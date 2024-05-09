
# CBUILD
A bit of a (long shot) alternative to premake or cmake or makefiles. It's kind of Nob (by [Tsoding](https://www.youtube.com/@tsoding) on YouTube).
The basic idea is that you specify the project layout in form of an XML file (a common file format), and then use `cbuild` to build your project (assuming you already have a compiler installed).

#### Example
Let's say you have a folder. Inside the folder you have a subfolder called _bin_ (within which you have another called _Debug_), an xml file called `Project.xml` and a `Main.cpp` (with a main function defined).
1. **Project.xml**
```xml
<Workspace Name="ProjectX">
    <OutputDir>bin</OutputDir>
    <IntermediateDir>obj</IntermediateDir>

    <Project Name="ProjectX" Kind="ConsoleApp" Arch="x64" Language="C++"  CppVersion="17" Compiler="g++">
        <Configuration Name="Debug" />
        <Flags>
            <Item Configuration="Debug">Og</Item>
            <Item>-Wall</Item>
        </Flags>
        <Defines>
            <Item>PX_DEBUG</Item>
        </Defines>
        
        <SourceDirs>
            <Item>./</Item> <!-- Current directory is a source folder -->
        </SourceDirs>
    </Project>
</Workspace>
```
2. **Main.cpp**
```cpp
int main()
{
#ifdef PX_DEBUG
    const char* config = "DEBUG";
#else
    const char* config = "RELEASE";
#endif // PX_DEBUG
    printf("[%s]: Hello, World from Cbuild & ProjectX\n\n", config);
    return 0;
}
```

3. Then we could use `cbuild` to build our Main.cpp on the command line (for now the `--config` is required, it maybe optional in the future; it has only one argument which is the confgiuration to use when building and these are defined in the Project.xml file)
```batch
cbuild Project.xml --config Debug
```

****

#### TODO
A lot of work😅

- [x] Implement use of special variables (a la Visual Studio, premake) e.g. `$(Configuration)`. In this case $(Configuration) = Debug | Release; this would be used in tags such as `<LibraryDirs>` and `<SourceDirs>`.
    ```xml
    <!-- ... -->
    <LibraryDirs>
        <Item>bin\$(Configuration)</Item>
    </LibraryDirs>
    <!-- ... -->
    ```
  
- [ ] Building of dependencies before dependents, e.g. below, Driver needs Api, so Api has to be built before, NOT AFTER.
- [x] Put `<Defines>` in one place in the `<Project>`, and not have definitions separated depending on the configuration. The configuration would be defined in the `<Item>` tag within the `<Defines>`. (The last define configuration-independent).
    ```xml
    <Project>
        <!-- ... -->
        <Defines>
            <Item Configuration="Debug">API_DEBUG</Item>
            <Item Configuration="Release">API_RELEASE</Item>
            <Item>API_WIN32</Item>
        </Defines>
        <!-- ... -->
    </Project>
    ```
- [ ] Cleaning (deleting files from output directories) and checking of output files in their respective directories before building to optimizing building/compiling process
- [ ] (**OPTIONAL**, *Sounds interesting*) Filewatcher for XML project file, to auto-build on modified; or filewatch the CWD to auto-build on any file modified.


