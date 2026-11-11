/**
 * \file posal_linux_signal.c
 *
 * \brief
 *  	This file contains the APIs implementation for posal signal using Linux
 * 		signal APIs
 *
 * \copyright
 *      Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *      SPDX-License-Identifier: BSD-3-Clause
 */
/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#include "posal.h"
#include "posal_linux_signal.h"

/* ----------------------------------------------------------------------------
 * Global Declarations/Definitions
 * ------------------------------------------------------------------------- */
//#define DEBUG_POSAL_LINUX_SIGNAL


/* -------------------------------------------------------------------------
 * Function Definitions
 * ------------------------------------------------------------------------- */
ar_result_t posal_linux_signal_create(posal_linux_signal_t *signal)
{
	ar_result_t status = AR_EOK;
    int32_t rc;

#ifdef DEBUG_POSAL_LINUX_SIGNAL
    AR_MSG(DBG_MED_PRIO, "Posal linux signal create");
#endif

#ifdef SAFE_MODE
    if (NULL == signal)
        return AR_EBADPARAM;
#endif

    posal_linux_signal_internal_t *signal_handles = (posal_linux_signal_internal_t *)malloc(sizeof(posal_linux_signal_internal_t));
    if (NULL == signal_handles)
    {
        return AR_ENOMEMORY;
    }

    pthread_cond_t* created_signal = (pthread_cond_t *)&signal_handles->created_signal;
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    rc = pthread_cond_init(created_signal, &attr);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to init cond, rc = %d\n", __func__, rc);
        status = AR_EFAILED;
        free(signal_handles);
        return status;
    }

    rc = pthread_condattr_destroy(&attr);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to init cond, rc = %d\n", __func__, rc);
        status = AR_EFAILED;
        goto fail;
    }


    signal_handles->signalled = 0;

    rc = pthread_mutex_init(&signal_handles->mutex_handle, NULL);
    if (rc) {
        status = AR_EFAILED;
        AR_MSG(DBG_ERROR_PRIO,"%s: failed to initialize mutex\n", __func__);
        goto fail;
    }

    *signal = (posal_linux_signal_t)signal_handles;
#ifdef DEBUG_POSAL_LINUX_SIGNAL
        AR_MSG(DBG_MED_PRIO, "Posal linux signal create exit");
#endif

    return status;

fail:
    pthread_cond_destroy(created_signal);
    free(signal_handles);
    return status;
}

ar_result_t posal_linux_signal_destroy(posal_linux_signal_t *signal)
{
	posal_linux_signal_internal_t *signal_handles = (posal_linux_signal_internal_t *)*signal;
	ar_result_t status = AR_EOK;
    int32_t rc;

#ifdef DEBUG_POSAL_LINUX_SIGNAL
    AR_MSG(DBG_MED_PRIO, "Posal linux signal destroy");
#endif

#ifdef SAFE_MODE
    if (NULL == signal)
    {
        status = AR_EBADPARAM;
        return status;
    }
#endif

    pthread_cond_t* created_signal = (pthread_cond_t *)&signal_handles->created_signal;

    rc = pthread_mutex_lock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to lock mutex\n", __func__);
        return AR_EFAILED;
    }

    signal_handles->signalled = 0;
    pthread_cond_destroy(created_signal);

    rc = pthread_mutex_unlock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to release mutex\n", __func__);
        return AR_EFAILED;
    }

    rc = pthread_mutex_destroy(&signal_handles->mutex_handle);

    free(signal_handles);

#ifdef DEBUG_POSAL_LINUX_SIGNAL
    AR_MSG(DBG_MED_PRIO, "Posal linux signal destroy exit");
#endif

exit:
    return status;
}

ar_result_t posal_linux_signal_clear(posal_linux_signal_t *signal, uint32_t signal_bitmask)
{
    posal_linux_signal_internal_t *signal_handles = (posal_linux_signal_internal_t *)(*signal);
    int32_t rc;

#ifdef DEBUG_POSAL_LINUX_SIGNAL
    AR_MSG(DBG_MED_PRIO, "Posal linux signal clear");
#endif

#ifdef SAFE_MODE
    if (NULL == signal)
    {
        return AR_EBADPARAM;
    }
#endif

    pthread_cond_t* created_signal = (pthread_cond_t *)&signal_handles->created_signal;

    rc = pthread_mutex_lock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to lock mutex\n", __func__);
        return AR_EFAILED;
    }

    signal_handles->signalled &= ~signal_bitmask;

    rc = pthread_mutex_unlock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to release mutex\n", __func__);
        return AR_EFAILED;
    }

#ifdef DEBUG_POSAL_LINUX_SIGNAL
    AR_MSG(DBG_MED_PRIO, "Posal linux signal clear exit");
#endif

    return AR_EOK;
}

uint32_t posal_linux_signal_wait(posal_linux_signal_t *signal, uint32_t signal_mask)
{
    posal_linux_signal_internal_t *signal_handles = (posal_linux_signal_internal_t *)(*signal);
    uint32_t return_signals = 0;
    int32_t rc;

#ifdef DEBUG_POSAL_LINUX_SIGNAL
    AR_MSG(DBG_MED_PRIO, "posal_linux_signal_wait: signal_ptr=0x%p, mask=0x%x", signal_handles, signal_mask);
#endif

    pthread_cond_t* created_signal = (pthread_cond_t *)&signal_handles->created_signal;

    if (signal_mask == 0)
    {
        return 0;
    }

    rc = pthread_mutex_lock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to lock mutex\n", __func__);
        return return_signals;
    }

    while ((signal_handles->signalled & signal_mask) == 0)
    {
        rc = pthread_cond_wait(created_signal, &signal_handles->mutex_handle);
        if (rc) {
            AR_MSG(DBG_ERROR_PRIO,"%s: Failed to wait on signal, rc = %d\n", __func__, rc);
            goto done;
        }
    }

    return_signals = signal_handles->signalled & signal_mask;

done:
    rc = pthread_mutex_unlock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to release mutex\n", __func__);
    }
#ifdef DEBUG_POSAL_LINUX_SIGNAL
    AR_MSG(DBG_MED_PRIO, "Exiting posal_linux_signal_wait");
#endif

    return return_signals;
}

ar_result_t posal_linux_signal_set(posal_linux_signal_t *signal, uint32_t signal_mask)
{
	posal_linux_signal_internal_t *signal_handles = (posal_linux_signal_internal_t *)*signal;
	ar_result_t status = AR_EOK;
    int32_t rc;
    uint32_t statuslock;

#ifdef SAFE_MODE
    if (NULL == signal)
    {
        status = AR_EBADPARAM;
        return status;
    }
#endif

    pthread_cond_t* created_signal = (pthread_cond_t *)&signal_handles->created_signal;

#ifdef DEBUG_POSAL_LINUX_SIGNAL
    AR_MSG(DBG_MED_PRIO, "Entering into posal_linux_signal_set");
    AR_MSG(DBG_MED_PRIO, "posal_linux_signal_set: setting event function 0x%p", signal_handles);
#endif

    rc = pthread_mutex_lock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to lock mutex\n", __func__);
        return AR_EFAILED;
    }

    signal_handles->signalled |= signal_mask;

    rc = pthread_cond_broadcast(created_signal);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO, "%s: Failed to signal on signal, rc = %d\n", __func__, rc);
        status = AR_EFAILED;
    }

    rc = pthread_mutex_unlock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to release mutex\n", __func__);
        return AR_EFAILED;
    }
done:
    return status;
}

uint32_t posal_linux_signal_get(posal_linux_signal_t *signal)
{
    uint32_t current_signals = 0;
    int32_t rc;
	posal_linux_signal_internal_t *signal_handles = (posal_linux_signal_internal_t *)*signal;

    rc = pthread_mutex_lock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to lock mutex\n", __func__);
        return current_signals;
    }

    current_signals = signal_handles->signalled;

    rc = pthread_mutex_unlock(&signal_handles->mutex_handle);
    if (rc) {
        AR_MSG(DBG_ERROR_PRIO,"%s: Failed to release mutex\n", __func__);
    }

    return current_signals;
}