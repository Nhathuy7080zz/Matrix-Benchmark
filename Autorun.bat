@echo off
gcc -O3 Single.c -o Single.exe
gcc -O3 Multi.c -o Multi.exe
gcc -O3 GPU.c -o GPU.exe -I.\OpenCL-Headers -DCL_TARGET_OPENCL_VERSION=120 C:\Windows\System32\OpenCL.dll
.\Single
.\Multi
.\GPU
pause