# CPU Matrix Benchmark

## Yêu cầu hệ thống
- Windows 10/11 (64-bit)
- CPU hỗ trợ tính toán đa luồng
- GPU hỗ trợ OpenCL 1.2
- Đã cài OpenCL runtime (thường đi kèm driver GPU)
- GCC (khuyến nghị MinGW-w64/MSYS2)

## Thư viện và phụ thuộc
- OpenCL headers: `OpenCL-Headers/`
- OpenCL loader/runtime: `C:\Windows\System32\OpenCL.dll`

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
- Nguồn: https://github.com/KhronosGroup/OpenCL-Headers
- Repo sử dụng: `OpenCL-Headers/`
- License: Apache License 2.0 (xem `OpenCL-Headers/LICENSE`)
