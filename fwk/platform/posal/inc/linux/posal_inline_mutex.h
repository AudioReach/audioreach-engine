/**
 * \file posal_mutex_inline.h
 * \brief
 *  	 An alterntitive mutex api for statically linked libraries to have lower memory overhead
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef POSAL_MUTEX_INLINE_H
#define POSAL_MUTEX_INLINE_H

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "ar_defs.h"
#include "ar_error_codes.h"
#include "posal_internal_inline.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus


typedef pthread_mutex_t  *posal_inline_mutex_t;

/**
 Initializes a mutex. Recursive mutexes are always used.

@datatypes
posal_mutex_t

@param[in] posal_mutex pointer to the mutex object handle.

@return
0 -- Success
@par
Nonzero -- Failure

@dependencies
None. @newpage
*/
static inline ar_result_t posal_inline_mutex_init(posal_inline_mutex_t *pposal_mutex)
{
    pthread_mutex_t* mutex = NULL;
    pthread_mutexattr_t attr;
    mutex = ((pthread_mutex_t *) malloc(sizeof(pthread_mutex_t)));
    if (NULL == mutex)
    {
        return AR_ENOMEMORY;
    }
    pthread_mutexattr_init(&attr);
#if defined(ARSPF_PLATFORM_QNX)
    pthread_mutexattr_setrecursive(&attr, PTHREAD_RECURSIVE_ENABLE);
#else
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
#endif
    pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
	*pposal_mutex = mutex;

    return AR_EOK;
}

/**
 Deinits a mutex. This function must be called for each corresponding
 posal_mutex_init() function to clean up all resources.

 @datatypes
 #posal_mutex_t

 @param[in] pposal_mutex   Pointer to the mutex to destroy.

 @return
 None.

 @dependencies
 The object must have been created and initialized before calling this
 function.
 */
static inline void posal_inline_mutex_deinit(posal_inline_mutex_t *pposal_mutex)
{
    pthread_mutex_t* mutex;
    mutex = *pposal_mutex;
    pthread_mutex_destroy(mutex);
    free(mutex);
    *pposal_mutex = NULL;
}


#ifdef __cplusplus
}
#endif //__cplusplus

#endif //POSAL_MUTEX_INLINE_H
