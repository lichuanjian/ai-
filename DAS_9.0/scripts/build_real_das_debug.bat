@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
set "PATH=D:\qt\Tools\QtCreator\bin\jom;D:\qt\Tools\CMake_64\bin;%PATH%"
"D:\qt\Tools\CMake_64\bin\cmake.exe" --build "E:\DAS_9.0(1)\DAS_9.0\build\Desktop_Qt_6_5_3_MSVC2019_64bit-Debug" --parallel
