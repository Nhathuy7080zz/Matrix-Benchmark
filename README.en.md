# CPU Matrix Benchmark

## System Requirements
- Windows 10/11 (64-bit)
- CPU with multithreading support
- GPU with OpenCL 1.2 support
- OpenCL runtime installed (usually included with the GPU driver)
- GCC (recommended: MinGW-w64/MSYS2)

## Libraries and Dependencies
- OpenCL headers: `OpenCL-Headers/`
- OpenCL loader/runtime: `C:\Windows\System32\OpenCL.dll`

## Autorun
Just execute Autorun.bat

## Build
CPU Single:
```bash
gcc -O3 Single.c -o Single.exe
```

CPU Multi:
```bash
gcc -O3 Multi.c -o Multi.exe
```

GPU:
```bash
gcc -O3 GPU.c -o GPU.exe -I.\OpenCL-Headers -DCL_TARGET_OPENCL_VERSION=120 C:\Windows\System32\OpenCL.dll
```

## OpenCL-Headers
- Source: https://github.com/KhronosGroup/OpenCL-Headers
- Used repository: `OpenCL-Headers/`
- License: Apache License 2.0 (see `OpenCL-Headers/LICENSE`)

## Notes
- Make sure the source file `gpu_bench.c` exists in the project folder before compiling.
- If the current source file name is different, replace `gpu_bench.c` with the actual file name.
