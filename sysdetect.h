#ifndef SYSDETECT_H
#define SYSDETECT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
        #include <intrin.h>
    #endif
#else
    #include <time.h>
#endif

// -- CROSS-PLATFORM TIMER --
static double get_time(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
#endif
}

// -- WINDOWS HARDWARE DETECTION --
#ifdef _WIN32

static void win_trim(char *s) {
    size_t l = strlen(s);
    while (l && (s[l-1]=='\n'||s[l-1]=='\r'||s[l-1]==' ')) s[--l]='\0';
    char *p = s; while (*p==' ') p++;
    if (p != s) memmove(s, p, strlen(p)+1);
}

static void get_os_info(char *os_name, size_t bufsz) {
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        char prod[256] = {0};
        char build[64] = {0};
        DWORD ubr = 0;
        
        DWORD sz = sizeof(prod);
        RegQueryValueExA(hkey, "ProductName", NULL, NULL, (LPBYTE)prod, &sz);
        
        sz = sizeof(build);
        RegQueryValueExA(hkey, "CurrentBuild", NULL, NULL, (LPBYTE)build, &sz);
        
        sz = sizeof(ubr);
        RegQueryValueExA(hkey, "UBR", NULL, NULL, (LPBYTE)&ubr, &sz);
        
        if (strncmp(prod, "Windows 10", 10) == 0 && atoi(build) >= 22000) {
            prod[8] = '1';
            prod[9] = '1';
        }
        
        snprintf(os_name, bufsz, "%s build %s.%lu", prod, build, ubr);
        RegCloseKey(hkey);
    } else {
        strncpy(os_name, "Windows", bufsz - 1);
    }
}

static void get_cpu_info(char *model, int *cores, int *threads, double *base_mhz) {
    model[0] = '\0';
    *cores = *threads = 0;
    *base_mhz = 0;

    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        char buf[256]; DWORD sz = sizeof(buf);
        if (RegQueryValueExA(hkey, "ProcessorNameString", NULL, NULL,
                             (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
            win_trim(buf);
            strncpy(model, buf, 255);
        }
        RegCloseKey(hkey);
    }
    if (model[0] == '\0') strcpy(model, "Unknown");

    HMODULE hPowrProf = LoadLibraryA("powrprof.dll");
    if (hPowrProf) {
        typedef NTSTATUS (WINAPI *PCALLNTPOWERINFORMATION)(
            int InformationLevel, PVOID InputBuffer, ULONG InputBufferLength,
            PVOID OutputBuffer, ULONG OutputBufferLength
        );
        PCALLNTPOWERINFORMATION pCallNtPowerInfo = 
            (PCALLNTPOWERINFORMATION)GetProcAddress(hPowrProf, "CallNtPowerInformation");
            
        if (pCallNtPowerInfo) {
            SYSTEM_INFO si; GetSystemInfo(&si);
            int cpu_count = si.dwNumberOfProcessors;
            
            typedef struct _PROCESSOR_POWER_INFORMATION {
                ULONG Number; ULONG MaxMhz; ULONG CurrentMhz;
                ULONG MhzLimit; ULONG MaxIdleState; ULONG CurrentIdleState;
            } PROCESSOR_POWER_INFORMATION;
            
            size_t buf_size = cpu_count * sizeof(PROCESSOR_POWER_INFORMATION);
            PROCESSOR_POWER_INFORMATION *ppi = (PROCESSOR_POWER_INFORMATION *)malloc(buf_size);
            
            if (ppi) {
                if (pCallNtPowerInfo(11, NULL, 0, ppi, (ULONG)buf_size) == 0) {
                    ULONG highest_max = 0;
                    for (int i = 0; i < cpu_count; i++) {
                        if (ppi[i].MaxMhz > highest_max) highest_max = ppi[i].MaxMhz;
                    }
                    if (highest_max > 0) *base_mhz = (double)highest_max;
                }
                free(ppi);
            }
        }
        FreeLibrary(hPowrProf);
    }

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    if (*base_mhz == 0) {
        int cpuInfo[4] = {0};
        __cpuid(cpuInfo, 0);
        if (cpuInfo[0] >= 0x16) {
            __cpuid(cpuInfo, 0x16);
            unsigned int ebx_max_mhz = cpuInfo[1] & 0xFFFF;
            if (ebx_max_mhz > 0) *base_mhz = (double)ebx_max_mhz;
        }
    }
#endif

    if (*base_mhz == 0) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hkey) == ERROR_SUCCESS) {
            DWORD mhz = 0; DWORD dsz = sizeof(mhz);
            if (RegQueryValueExA(hkey, "~MHz", NULL, NULL,
                                 (LPBYTE)&mhz, &dsz) == ERROR_SUCCESS) {
                *base_mhz = (double)mhz;
            }
            RegCloseKey(hkey);
        }
    }

    SYSTEM_INFO si; GetSystemInfo(&si);
    *threads = (int)si.dwNumberOfProcessors;

    DWORD sz = 0;
    GetLogicalProcessorInformation(NULL, &sz);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf = malloc(sz);
    if (buf) {
        if (GetLogicalProcessorInformation(buf, &sz)) {
            int n = (int)(sz / sizeof(*buf));
            for (int i = 0; i < n; i++)
                if (buf[i].Relationship == RelationProcessorCore) (*cores)++;
        }
        free(buf);
    }
    if (*cores == 0) *cores = *threads;
}

static void get_ram_info(unsigned long long *total_mb) {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    *total_mb = ms.ullTotalPhys / (1024ULL*1024ULL);
}

static void get_gpu_info(char *gpu1, char *gpu2, size_t bufsz) {
    gpu1[0] = gpu2[0] = '\0';
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD index = 0;
        char subkey[256];
        DWORD subkey_sz = sizeof(subkey);
        int gpu_count = 0;
        
        while (RegEnumKeyExA(hkey, index, subkey, &subkey_sz, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            HKEY sub;
            if (RegOpenKeyExA(hkey, subkey, 0, KEY_READ, &sub) == ERROR_SUCCESS) {
                char desc[256] = {0};
                DWORD sz = sizeof(desc);
                if (RegQueryValueExA(sub, "DriverDesc", NULL, NULL, (LPBYTE)desc, &sz) == ERROR_SUCCESS) {
                    if (desc[0] != '\0' && gpu_count < 2) {
                        if (gpu_count == 0) strncpy(gpu1, desc, bufsz - 1);
                        else                strncpy(gpu2, desc, bufsz - 1);
                        gpu_count++;
                    }
                }
                RegCloseKey(sub);
            }
            index++;
            subkey_sz = sizeof(subkey);
        }
        RegCloseKey(hkey);
    }
    if (gpu1[0] == '\0') strncpy(gpu1, "N/A", bufsz - 1);
}

static void get_l3_cache(unsigned long long *l3_kb) {
    *l3_kb = 0;
    DWORD sz = 0;
    GetLogicalProcessorInformation(NULL, &sz);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf = malloc(sz);
    if (buf) {
        if (GetLogicalProcessorInformation(buf, &sz)) {
            int n = (int)(sz / sizeof(*buf));
            for (int i = 0; i < n; i++) {
                if (buf[i].Relationship == RelationCache &&
                    buf[i].Cache.Level == 3) {
                    *l3_kb += buf[i].Cache.Size / 1024ULL;
                }
            }
        }
        free(buf);
    }
}

// -- LINUX HARDWARE DETECTION --
#else

static void get_os_info(char *os_name, size_t bufsz) {
    strncpy(os_name, "Linux", bufsz - 1);
    FILE *f = fopen("/etc/os-release", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *p = line + 12;
                if (*p == '"') p++;
                size_t l = strlen(p);
                while (l && (p[l-1] == '\n' || p[l-1] == '\r' || p[l-1] == '"')) p[--l] = '\0';
                strncpy(os_name, p, bufsz - 1);
                break;
            }
        }
        fclose(f);
    }
}

static void get_cpu_info(char *model, int *cores, int *threads, double *base_mhz) {
    FILE *f;
    char line[256];
    model[0] = '\0';
    *cores = *threads = 0;
    *base_mhz = 0;

    f = fopen("/proc/cpuinfo", "r");
    if (f) {
        int pkg_cores = 0, pkg_count = 0, last_phys = -1;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "model name", 10) == 0 && model[0] == '\0') {
                char *p = strchr(line, ':');
                if (p) {
                    p += 2;
                    size_t l = strlen(p);
                    if (l && p[l-1] == '\n') p[l-1] = '\0';
                    strncpy(model, p, 255);
                }
            }
            if (strncmp(line, "processor", 9) == 0) (*threads)++;
            if (strncmp(line, "cpu cores", 9) == 0 && pkg_cores == 0) {
                char *p = strchr(line, ':');
                if (p) pkg_cores = atoi(p + 2);
            }
            if (strncmp(line, "physical id", 11) == 0) {
                char *p = strchr(line, ':');
                if (p) {
                    int phys = atoi(p + 2);
                    if (phys != last_phys) { pkg_count++; last_phys = phys; }
                }
            }
        }
        fclose(f);
        if (pkg_cores > 0) *cores = pkg_cores * (pkg_count > 0 ? pkg_count : 1);
    }
    if (*cores == 0) *cores = *threads;
    if (model[0] == '\0') strcpy(model, "Unknown");
    
    char *at_sign = strstr(model, "@ ");
    if (at_sign) {
        *base_mhz = atof(at_sign + 2) * 1000.0;
    }

    if (*base_mhz == 0) {
        f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency", "r");
        if (!f) f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
        if (f) {
            unsigned long khz = 0;
            if (fscanf(f, "%lu", &khz) == 1) *base_mhz = khz / 1000.0;
            fclose(f);
        }
    }
    
    if (*base_mhz == 0) {
        f = fopen("/proc/cpuinfo", "r");
        if (f) {
            while (fgets(line, sizeof(line), f))
                if (strncmp(line, "cpu MHz", 7) == 0) {
                    char *p = strchr(line, ':');
                    if (p) { *base_mhz = atof(p + 2); break; }
                }
            fclose(f);
        }
    }
}

static void get_ram_info(unsigned long long *total_mb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { *total_mb = 0; return; }
    char line[128];
    unsigned long long total_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9,  "%llu", &total_kb);
            break;
        }
    }
    fclose(f);
    *total_mb = total_kb / 1024;
}

static void get_gpu_info(char *gpu1, char *gpu2, size_t bufsz) {
    gpu1[0] = gpu2[0] = '\0';
    FILE *fp = popen("lspci 2>/dev/null | grep -iE 'vga|display|3d'", "r");
    if (fp) {
        char line[256]; int count = 0;
        while (fgets(line, sizeof(line), fp) && count < 2) {
            char *p = strrchr(line, ':');
            p = p ? p + 2 : line;
            size_t l = strlen(p);
            if (l && p[l-1] == '\n') p[l-1] = '\0';
            if (count == 0) strncpy(gpu1, p, bufsz - 1);
            else            strncpy(gpu2, p, bufsz - 1);
            count++;
        }
        pclose(fp);
    }
    if (gpu1[0] == '\0') strcpy(gpu1, "N/A (install: pciutils)");
}

static void get_l3_cache(unsigned long long *l3_kb) {
    *l3_kb = 0;
    for (int i = 0; i < 8; i++) {
        char path[128];
        snprintf(path, sizeof(path),
            "/sys/devices/system/cpu/cpu0/cache/index%d/level", i);
        FILE *f = fopen(path, "r");
        if (!f) break;
        int level = 0;
        fscanf(f, "%d", &level);
        fclose(f);
        if (level == 3) {
            snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu0/cache/index%d/size", i);
            f = fopen(path, "r");
            if (f) {
                char buf[32] = {0};
                fscanf(f, "%31s", buf);
                fclose(f);
                unsigned long long val = strtoull(buf, NULL, 10);
                size_t l = strlen(buf);
                char unit = l ? buf[l - 1] : 0;
                if      (unit == 'K' || unit == 'k') *l3_kb = val;
                else if (unit == 'M' || unit == 'm') *l3_kb = val * 1024ULL;
                else                                 *l3_kb = val / 1024ULL;
            }
            break;
        }
    }
}

#endif /* _WIN32 */

static int print_sysinfo(void) {
    char cpu_model[256], gpu1[256], gpu2[256], os_name[256];
    int cores, threads;
    double base_mhz;
    unsigned long long ram_total, l3_kb;

    get_os_info(os_name, sizeof(os_name));
    get_cpu_info(cpu_model, &cores, &threads, &base_mhz);
    get_ram_info(&ram_total);
    get_gpu_info(gpu1, gpu2, sizeof(gpu1));
    get_l3_cache(&l3_kb);

    printf("=========================================\n");
    printf("           THONG TIN HE THONG\n");
    printf("=========================================\n");
    printf("OS     : %s\n", os_name);
    printf("CPU    : %s\n", cpu_model);
    printf("         Cores: %d  |  Threads: %d  |  Base: %.0f MHz\n", cores, threads, base_mhz);
    if (l3_kb > 0)
        printf("         L3 Cache: %llu MB\n", l3_kb / 1024ULL);
    printf("RAM    : %llu MB\n", ram_total);
    printf("GPU 1  : %s\n", gpu1);
    if (gpu2[0]) printf("GPU 2  : %s\n", gpu2);
    printf("=========================================\n\n");
    
    return threads;
}

#endif // SYSDETECT_H