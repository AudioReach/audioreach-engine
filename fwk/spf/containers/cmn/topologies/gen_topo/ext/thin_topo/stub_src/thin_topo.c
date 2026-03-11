/**
 * \file thin_topo.c
 * \brief
 *     This file contains thin topo stub functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"

ar_result_t thin_topo_init_handle(gen_topo_t *topo_ptr)
{
   return AR_EOK;
}

void thin_topo_reset_handle(gen_topo_t *topo_ptr, bool_t free_handle_memory)
{
   return;
}

void thin_topo_reset_signal_tgp_flag(gen_topo_t *topo_ptr)
{
   return;
}

void thin_topo_update_exit_flags(gen_topo_t *topo_ptr,
                                 bool_t      has_voice_sg,
                                 bool_t      need_soft_timer_extn,
                                 bool_t      has_duty_cycling_module,
                                 bool_t      has_signal_tgp_module,
                                 bool_t      requires_module_looping,
                                 bool_t      has_sh_mem_ep_module)
{
   return;
}

void thin_topo_destroy(gen_topo_t *topo_ptr)
{
   return;
}
