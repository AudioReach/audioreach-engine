#ifndef THIN_TOPO_H
#define THIN_TOPO_H
/**
 * \file thin_topo.h
 * \brief
 *     This file contains utility functions for Thin topo.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "ar_defs.h"

// clang-format off

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

#ifdef VERBOSE_DEBUGGING

#define THIN_TOPO_TS_DISC_DEBUG
#define THIN_TOPO_SAFE_MODE
#define THIN_TOPO_PROCESS_BUF_DEBUG
#define THIN_TOPO_BUF_ASSIGNMENT_DEBUG

#endif

#define THIN_TOPO_STATIC static

typedef union thin_topo_exit_flags_t
{
   struct
   {
      /** STATIC PROPERTY FLAGS
            flags based on static properties or determined at graph open/close will be
            updated at gen_topo_reset_top_level_flags()
       */
      uint32_t has_more_than_one_sg:1;       // BIT 0
      uint32_t has_voice_sg:1;               // BIT 1
      uint32_t has_parallel_paths:1;         // BIT 2
      uint32_t is_signal_trigger_deactive:1; // BIT 3

       // BIT 4-7
      uint32_t has_sync_module:1;            // BIT 4
      uint32_t has_active_dm_module:1;       // BIT 5
      uint32_t has_duty_cycling_module:1;    // BIT 6
      uint32_t has_soft_timer_module:1;      // BIT 7
      /** END OF STATIC PROPERTY FLAGS */

      // BIT 8-11
      // flags based on dynamic events
      uint32_t has_active_data_tgp_extn:1;   // BIT 8
      uint32_t has_active_signal_tgp_extn:1; // BIT 9
      uint32_t thresh_prop_not_complete:1;   // BIT 10
      uint32_t requires_module_looping:1;    // BIT 11

      // BIT 12-15
      uint32_t has_sh_mem_ep_module:1;                      // BIT 12 - Thin topo doesnt support shared memory end point module's as of now
      uint32_t has_to_preserve_prebuffer:1;
      uint32_t num_active_md_nodes_in_cntr:8;  /**< Number of metadata currently present in the container, it can be in the cntr/topo/CAPI modules. */

      //uint32_t has_mixed_data_flow_states:1;
            /**< modules with started and AT_GAP cannot be handled in thin topo, because it requires thin topo to check data flow states for the output ports*/

      // uint32_t has_pending_mf:1;
            //todo: check if this is required ? in STM cntrs mF gets proapgated immediately normally. what about ext output ports ?

      //uint32_t has_non_pcm_modules:1;
            // todo: seems like we can support non pcm modules as long as they dont hold partial data.
   };
   uint64_t word;

}thin_topo_exit_flags_t;

typedef enum thin_topo_state_t
{
   THIN_TOPO_SWITCHED_TO_GEN_TOPO                = 0,
   THIN_TOPO_EXITED_AT_EXT_IN_BUFFER_SETUP       = 1,
   THIN_TOPO_EXITED_AT_EXT_OUT_BUFFER_SETUP      = 2,
   THIN_TOPO_EXITED_AT_MODULE_PROCESS_EVENTS     = 3,
   THIN_TOPO_EXITED_AT_OUTPUT_POST_PROCESS       = 4,
   THIN_TOPO_ENTERED                             = 5,
}thin_topo_state_t;

typedef struct thin_topo_t
{
   gu_module_list_t                 *active_module_list_ptr; /**< subset of started_sorted_modue_list, but only includes modules from started-SG */
   gu_ext_in_port_list_t             *active_ext_in_list_ptr; /**< only started ext in ports */
   gu_ext_out_port_list_t            *active_ext_out_list_ptr; /**< only started ext out ports */

   thin_topo_state_t                 state; /** indicates at what point of thin topo processing thin topo was exited. */
   gu_module_list_t                 *rest_of_module_proc_list_ptr; /**< indicates if there has been a switch from thin topo to gen topo due to some event */
}thin_topo_t;

ar_result_t thin_topo_init_handle(gen_topo_t *topo_ptr);
void thin_topo_reset_handle(gen_topo_t *topo_ptr, bool_t free_handle_memory);

void thin_topo_reset_signal_tgp_flag(gen_topo_t *topo_ptr);

void thin_topo_update_exit_flags(gen_topo_t *topo_ptr,
                           bool_t      has_voice_sg,
                           bool_t      need_soft_timer_extn,
                           bool_t      has_duty_cycling_module,
                           bool_t      has_signal_tgp_module,
                           bool_t      requires_module_looping,
                           bool_t      has_sh_mem_ep_module);

void thin_topo_destroy(gen_topo_t *topo_ptr);

#if defined(__cplusplus)
}
#endif // __cplusplus

#endif /* THIN_TOPO_H */
