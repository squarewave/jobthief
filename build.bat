@echo OFF

call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64

cl jobthief_test.c -Od -Zi -WX -Wall -wd4820 -wd4200 -wd4100 -wd4189 -wd4710 -wd4702 -wd4711