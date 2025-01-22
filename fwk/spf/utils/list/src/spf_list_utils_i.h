#ifndef _SPF_LIST_UTILS_I_H__
#define _SPF_LIST_UTILS_I_H__

/**
 * \file spf_list_utils_i.h
 * \brief
 *     This file contains private declerations for List Utilities
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

// Number of pools of list nodes, same as number of heap ids it will use
#define SPF_LIST_NUM_POOLS (2)

spf_list_node_t *spf_list_create_new_node(void *obj_ptr, POSAL_HEAP_ID heap_id, bool_t use_pool);

uint32_t get_handle_from_heap_id(POSAL_HEAP_ID heap_id);

spf_list_node_t *spf_list_get_node(POSAL_HEAP_ID heap_id);

void spf_list_node_return(spf_list_node_t *node_ptr);

void spf_list_node_free(spf_list_node_t *node_ptr, bool_t pool_used);

bool_t spf_list_search_delete_node(spf_list_node_t **head_ptr, void *obj_ptr, bool_t to_delete, bool_t pool_used);

#ifdef __cplusplus
}
#endif //__cplusplus
#endif /* _SPF_LIST_UTILS_I_H__ */