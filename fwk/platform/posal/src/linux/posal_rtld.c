/**
 * \file posal_rtld.c
 * \brief
 *     This file contains wrappers for dl handling functions.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#define _GNU_SOURCE
#include "posal.h"
#include "posal_internal.h"
#include "posal_rtld.h"
#include <dlfcn.h>

/*--------------------------------------------------------------*/
/* Macro definitions                                            */
/* -------------------------------------------------------------*/

/* -----------------------------------------------------------------------
** Constant / Define Declarations
** ----------------------------------------------------------------------- */

/* =======================================================================
**                          Function Definitions
** ======================================================================= */

void* posal_dlopen(const char* name, int flags)
{
    return dlopen(name, flags);
}

void* posal_dlopenbuf(const char* name, const char* buf, int len, int flags)
{
    // dlopenbuf is not defined on all targets
    //return dlopenbuf(name, buf, len, flags);
    return 0;
}

int posal_dlclose(void* handle)
{
    return dlclose(handle);
}

void* posal_dlsym(void* handle, const char* name)
{
    return dlsym(handle, name);
}

char* posal_dlerror(void)
{
    return dlerror();
}

int posal_dlinfo(void* handle, int request, void* p)
{
    return dlinfo(handle, request, p);
}
