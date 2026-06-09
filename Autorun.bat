@echo off
if exist results.csv del results.csv

echo Compiling . . .
gcc -O3 Single.c -o Single.exe
gcc -O3 Multi.c -o Multi.exe
gcc -O3 GPU.c -o GPU.exe -I.\OpenCL-Headers -DCL_TARGET_OPENCL_VERSION=120 C:\Windows\System32\OpenCL.dll

cls
.\Single.exe
.\Multi.exe
.\GPU.exe
pause