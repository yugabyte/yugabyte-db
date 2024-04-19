Rem Update the PostgreSQL version here
Rem /!\ Don't forget to rebuild the PostgreSQL code AND install the proper version with the MSI installer !
SET PG_MAJOR_VERSION=12
SET PG_MINOR_VERSION=12.7

Rem This env variable may change depending on the Server version and the VS version
Rem Open a command prompt and type `SET` to update them
SET INCLUDE=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\ATLMFC\include;C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\include;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\include\um;C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\ucrt;C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\shared;C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\um;C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\winrt;C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\cppwinrt
SET LIB=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\ATLMFC\lib\x86;C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\lib\x86;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\lib\um\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.19041.0\ucrt\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.19041.0\um\x86
SET LIBPATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\ATLMFC\lib\x86;C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\lib\x86;C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\lib\x86\store\references;C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.19

Rem To update the compilation options : Open the VS project and go to Properties > Linker > Command Line
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\bin\Hostx64\x64\cl.exe" ^
  anon.c  ^
  /I"C:\Program Files\PostgreSQL\%PG_MAJOR_VERSION%\include\server\port\win32_msvc" ^
  /I"C:\Program Files\PostgreSQL\%PG_MAJOR_VERSION%\include\server\port\win32" ^
  /I"C:\Program Files\PostgreSQL\%PG_MAJOR_VERSION%\include\server\utils" ^
  /I"C:\Program Files\PostgreSQL\%PG_MAJOR_VERSION%\include\server\port" ^
  /I"C:\Program Files\PostgreSQL\%PG_MAJOR_VERSION%\include\server" ^
  /I"C:\Program Files\PostgreSQL\%PG_MAJOR_VERSION%\include" ^
  /GS- ^
  /GL ^
  /D "_WINDLL" ^
  /ifcOutput "x64\Release\" /GS /GL /W3 /Gy /Zc:wchar_t ^
  /Zi /Gm- /O2 /sdl /Fd"x64\Release\vc142.pdb" /FS /Zc:inline /fp:precise ^
  /D "NDEBUG" /D "_CONSOLE" /D "_WINDLL" /D "_UNICODE" /D "UNICODE" ^
  /errorReport:prompt ^
  /WX- /Zc:forScope /Gd /Oi /MT /FC /Fa"x64\Release\" /EHsc /nologo ^
  /Fo"anon.obj" /Fp"anon.pch" /diagnostics:column ^
  /link /SUBSYSTEM:CONSOLE ^
  /NOENTRY ^
  /OUT:"anon.dll" ^
  /MANIFEST ^
  /LTCG:incremental ^
  /NXCOMPAT ^
  /PDB:"anon.pdb" ^
  /DYNAMICBASE "postgres.lib" "kernel32.lib" "user32.lib" "gdi32.lib" "winspool.lib" "comdlg32.lib" "advapi32.lib" "shell32.lib" "ole32.lib" "oleaut32.lib" "uuid.lib" "odbc32.lib" "odbccp32.lib" ^
  /IMPLIB:"anon.lib" ^
  /DEBUG /DLL /MACHINE:X64 /OPT:REF /INCREMENTAL:NO ^
  /MANIFEST:NO ^
  /OPT:ICF ^
  /ERRORREPORT:PROMPT /ILK:"anon.ilk" ^
  /NOLOGO ^
  /LIBPATH:"C:\Program Files\PostgreSQL\%PG_MAJOR_VERSION%\lib" ^
  /TLBID:1

