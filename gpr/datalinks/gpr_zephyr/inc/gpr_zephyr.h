/*
 * gpr_zephyr.h
 *
 * This file has implementation platform wrapper for the GPR datalink layer
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gpr_comdef.h"
#include "ipc_dl_api.h"

/******************************************************************************
 * Defines                                                                    *
 *****************************************************************************/
/*IPC datalink init function called from gpr layer for zephyr using open-amp*/
GPR_INTERNAL uint32_t ipc_dl_zephyr_init(uint32_t                 src_domain_id,
                                        uint32_t                 dest_domain_id,
                                        const gpr_to_ipc_vtbl_t *p_gpr_to_ipc_vtbl,
                                        ipc_to_gpr_vtbl_t **     pp_ipc_to_gpr_vtbl);

/*IPC datalink de-init function called from gpr layer for zephyr using open-amp */
GPR_INTERNAL uint32_t ipc_dl_zephyr_deinit (uint32_t src_domain_id, uint32_t dest_domain_id);