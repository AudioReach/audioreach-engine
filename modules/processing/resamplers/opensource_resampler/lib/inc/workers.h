////////////////////////////////////////////////////////////////////////////
//                           **** WORKERS ****                            //
//                   Lightweight Worker Thread Manager                    //
//                Copyright (c) 2023 - 2025 David Bryant.                 //
//                          All Rights Reserved.                          //
//        Distributed under the BSD Software License (see LICENSE)        //
////////////////////////////////////////////////////////////////////////////

// workers.h
// SPF adaptation: removed <windows.h>/<process.h>/<pthread.h> platform
// threading headers — ENABLE_THREADS is not defined for SPF/Hexagon builds
// so all worker thread code in resampler.c is compiled out. Workers is used
// only as an opaque pointer in the Resample struct.

#ifndef WORKERS_H
#define WORKERS_H

#include <stdlib.h>
#include <stdint.h>

#define A_BEFORE_B(A,B) (((A)-(B)) & 0x80000000)
#define A_AFTER_B(A,B)  (((B)-(A)) & 0x80000000)

#ifdef ENABLE_THREADS
// Threading support not available on SPF — left here for completeness only.
// Define ENABLE_THREADS to enable multi-threaded resampling on host builds.
typedef void (*WorkerFunction)(void *context);
typedef enum { WaitForAvailableWorkerThread, DontUseWorkerThread } WorkerMode;
typedef struct Workers Workers;
Workers *workersInit(int numWorkerThreads);
void workersEnqueueJob(Workers *workers, WorkerFunction func, void *context, WorkerMode mode);
void workersWaitAllJobs(Workers *workers);
void workersDeinit(Workers *workers);
#else
typedef struct Workers Workers;
#endif

#endif
