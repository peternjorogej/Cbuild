
# CBUILD
A bit of a (long shot) alternative to premake or cmake or makefiles. It's kind of Nob (by [Tsoding](https://www.youtube.com/@tsoding) on YouTube).
The basic idea is that you specify the project layout in form of an XML file (a common file format), and then use `cbuild` to build your project (assuming you already have a compiler installed).

#### Example
Let's say you have a folder. Inside the folder you have a subfolder called _bin_ (within which you have another called _Debug_), an xml file called `Project.xml` and a `Main.cpp` (with a main function defined).
1. **Project.xml**
```xml
<Workspace Name="ProjectX">
    <OutputDir>bin</OutputDir>

    <Project Name="ProjectX" Kind="ConsoleApp" Arch="x64" Language="C++"  CppVersion="17" Compiler="g++">
        <Configuration Name="Debug">
            <Flags>
                <Item>Og</Item>
            </Flags>
            <Defines>
                <Item>PX_DEBUG</Item>
            </Defines>
            <!-- Expected: g++ -DPX_DEBUG -m64 -std=c++17 -Og .\Main.cpp -o bin\Debug\ProjectX.exe -->
        </Configuration>

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
    printf("Hello, World from Cbuild & ProjectX\n\n");
}
```

Then we could use `cbuild` to build our Main.cpp on the command line
```batch
cbuild Project.xml
```

****

#### TODO
A lot of work😅

