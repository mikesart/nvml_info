/*
 * Copyright 2019 Michael Sartain
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// NVML Api Reference:
//   https://docs.nvidia.com/deploy/nvml-api/nvml-api-reference.html
//
// nvidia-smi examples:
//   nvidia-smi -q
//   nvidia-smi pmon -s um
//   nvidia-smi pmon -o T -s um
//
// To build:
//   g++ nvml_info.c -gdwarf-4 -g2 -ldl -o nvml_info

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <vector>

#include "nvml_header.h"

/*
 * Structs
 */
typedef struct
{
    char name[ 80 ] = { 0 };
    unsigned int pid = 0;
    unsigned long long usedGpuMemory = 0;
} nvgpu_processinfo_t;

typedef struct
{
    nvmlBAR1Memory_t bar1mem = { 0 };
    nvmlMemory_t meminfo = { 0 };

    std::vector< nvgpu_processinfo_t > gpu_processinfos;
    std::vector< nvgpu_processinfo_t > compute_processinfos;
} nvgpu_devinfo_t;

typedef struct
{
    char driver_version[ 256 ] = { 0 };
    char nvml_version[ 256 ] = { 0 };
    int cuda_version = 0;

    std::vector< nvgpu_devinfo_t > devinfos;
} nvgpu_info_t;

/*
 * Function Prototypes
 */
int nvml_functions_init( nvml_functions_t &nvmlfuncs );
void nvml_functions_shutdown( nvml_functions_t &nvmlfuncs );

nvmlReturn_t nvgpu_get_info( const nvml_functions_t &nvmlfuncs, nvgpu_info_t &nvgpu_info );

/*
 * Functions
 */

// Declare empty error functions if symbol isn't found in dso
#define HOOK_FUNC( _ret, _reterr, _func, _args, ... ) static _ret ( _func ## _null )( __VA_ARGS__ ) { return _reterr; }
    #include "nvml_hook_funcs.inl"
#undef HOOK_FUNC

int nvml_functions_init( nvml_functions_t &nvmlfuncs )
{
    // Try to load nvml dso
    void *libnvml = dlopen( "libnvidia-ml.so.1", RTLD_NOW | RTLD_GLOBAL );
    if ( !libnvml )
        libnvml = dlopen( "libnvidia-ml.so", RTLD_NOW | RTLD_GLOBAL );
    if ( !libnvml )
    {
        printf( "ERROR: nvml_functions_init failed (%s)\n", dlerror() );
        return -1;
    }

    // Get nvml function addresses
#define HOOK_FUNC( _ret, _reterr, _func, _args, ... )           \
    nvmlfuncs._func = ( _func##_t * )dlsym( libnvml, #_func );  \
    if ( !nvmlfuncs._func )                                     \
        nvmlfuncs._func = _func ## _null;

    do {
        #include "nvml_hook_funcs.inl"
    } while ( 0 );

#undef HOOK_FUNC

    // Use v2 functions if available
    if ( nvmlfuncs.nvmlInit_v2 )
        nvmlfuncs.nvmlInit = nvmlfuncs.nvmlInit_v2;

    if ( nvmlfuncs.nvmlSystemGetCudaDriverVersion_v2 )
        nvmlfuncs.nvmlSystemGetCudaDriverVersion = nvmlfuncs.nvmlSystemGetCudaDriverVersion_v2;

    nvmlfuncs.libnvml = libnvml;
    return 0;
}

void nvml_functions_shutdown( nvml_functions_t &nvmlfuncs )
{
    if ( nvmlfuncs.libnvml )
    {
        dlclose( nvmlfuncs.libnvml );
        nvmlfuncs.libnvml = NULL;
    }
}

#define NVML_CALL( _ret, _func, ... )                                                       \
    do {                                                                                    \
        _ret = nvmlfuncs._func( __VA_ARGS__ );                                              \
        if ( _ret != NVML_SUCCESS )                                                         \
        {                                                                                   \
            printf( "ERROR: %s failed (%s)\n", #_func, nvmlfuncs.nvmlErrorString( _ret ) ); \
        }                                                                                   \
    } while ( 0 )

static void nvgpu_get_process_info( const nvml_functions_t &nvmlfuncs, nvmlDevice_t nvmldev, std::vector< nvgpu_processinfo_t > &dst, bool doCompute )
{
    nvmlReturn_t ret;

    unsigned int process_count = 256;
    nvmlProcessInfo_t process_infos[ 256 ];

    if ( doCompute )
        NVML_CALL( ret, nvmlDeviceGetComputeRunningProcesses, nvmldev, &process_count, process_infos );
    else
        NVML_CALL( ret, nvmlDeviceGetGraphicsRunningProcesses, nvmldev, &process_count, process_infos );

    if ( !ret && process_count )
    {
        dst.resize( process_count );

        for ( unsigned int i = 0; i < process_count; i++ )
        {
            nvgpu_processinfo_t &procinfo = dst[ i ];

            procinfo.pid = process_infos[ i ].pid;
            procinfo.usedGpuMemory = process_infos[ i ].usedGpuMemory;

            NVML_CALL( ret, nvmlSystemGetProcessName, process_infos[ i ].pid, procinfo.name, sizeof( procinfo.name ) );
        }
    }
}

nvmlReturn_t nvgpu_get_info( const nvml_functions_t &nvmlfuncs, nvgpu_info_t &nvgpu_info )
{
    nvmlReturn_t ret;

    ret = nvmlfuncs.nvmlInit();
    if ( ret != NVML_SUCCESS )
    {
        printf( "ERROR: nvmlInit failed (%s)\n", nvmlfuncs.nvmlErrorString( ret ) );
    }
    else
    {
        NVML_CALL( ret, nvmlSystemGetNVMLVersion, nvgpu_info.nvml_version, sizeof( nvgpu_info.nvml_version ) );
        NVML_CALL( ret, nvmlSystemGetDriverVersion, nvgpu_info.driver_version, sizeof( nvgpu_info.driver_version ) );
        NVML_CALL( ret, nvmlSystemGetCudaDriverVersion, &nvgpu_info.cuda_version );

        unsigned int device_count = 0;
        NVML_CALL( ret, nvmlDeviceGetCount, &device_count );

        if ( ret == NVML_SUCCESS )
        {
            for ( unsigned int i = 0; i < device_count; i++ )
            {
                nvmlDevice_t nvmldev;
                nvgpu_devinfo_t devinfo;

                NVML_CALL( ret, nvmlDeviceGetHandleByIndex, i, &nvmldev );

                NVML_CALL( ret, nvmlDeviceGetBAR1MemoryInfo, nvmldev, &devinfo.bar1mem );
                NVML_CALL( ret, nvmlDeviceGetMemoryInfo, nvmldev, &devinfo.meminfo );

                nvgpu_get_process_info( nvmlfuncs, nvmldev, devinfo.gpu_processinfos, false );
                nvgpu_get_process_info( nvmlfuncs, nvmldev, devinfo.compute_processinfos, true );

                nvgpu_info.devinfos.emplace_back( devinfo );
            }
        }

        nvmlfuncs.nvmlShutdown();
    }

    return ret;
}

void print_gpu_info( const nvgpu_info_t &nvgpu_info )
{
    const double invMB = 1.0 / ( 1024 * 1024 );

    printf( "Driver Version: %s\n", nvgpu_info.driver_version );
    printf( "NVML Version: %s\n", nvgpu_info.nvml_version );
    printf( "Cuda Version: %d.%d\n", NVML_CUDA_DRIVER_VERSION_MAJOR( nvgpu_info.cuda_version ), NVML_CUDA_DRIVER_VERSION_MINOR( nvgpu_info.cuda_version ) );

    int devno = 0;
    for ( const nvgpu_devinfo_t &devinfo : nvgpu_info.devinfos )
    {
        printf( "\nDevice #%d:\n", devno++ );

        printf( "  Bar1: %.2fMB used of %.2fMB\n", devinfo.bar1mem.bar1Used * invMB, devinfo.bar1mem.bar1Total * invMB );
        printf( "  Gpu:  %.2fMB used of %.2fMB\n", devinfo.meminfo.used * invMB, devinfo.meminfo.total * invMB );

        if ( !devinfo.gpu_processinfos.empty() )
        {
            printf( "  Gpu Processes:\n" );

            for ( const nvgpu_processinfo_t &procinfo : devinfo.gpu_processinfos )
            {
                printf( "    % 8.2fMB %s (%d)\n", procinfo.usedGpuMemory * invMB, procinfo.name, procinfo.pid );
            }
        }

        if ( !devinfo.compute_processinfos.empty() )
        {
            printf( "  Compute Processes:\n" );

            for ( const nvgpu_processinfo_t &procinfo : devinfo.compute_processinfos )
            {
                printf( "    % 8.2fMB %s (%d)\n", procinfo.usedGpuMemory * invMB, procinfo.name, procinfo.pid );
            }
        }
    }
}

int main( int argc, char *argv[] )
{
    nvml_functions_t nvmlfuncs;

    if ( nvml_functions_init( nvmlfuncs ) == 0 )
    {
        nvgpu_info_t nvgpu_info;

        nvgpu_get_info( nvmlfuncs, nvgpu_info );
        print_gpu_info( nvgpu_info );

        nvml_functions_shutdown( nvmlfuncs );
    }

    return 0;
}
