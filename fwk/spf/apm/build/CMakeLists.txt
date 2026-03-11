#[[
   @file CMakeLists.txt

   @brief
]]
cmake_minimum_required(VERSION 3.10)

#Include directories
include_directories(
                    ../core/src
                    ../ext/apm_db_query/inc
                    ../ext/close_all/inc
                    ../ext/cmn/inc
                    ../ext/cmn/src
                    ../ext/cntr_peer_heap_utils/inc
                    ../ext/data_path/inc
                    ../ext/debug_info_cfg/inc
                    ../ext/debug_info_dump/inc
                    ../ext/err_hdlr/inc
                    ../ext/gpr_cmd_rsp_hdlr/inc
                    ../ext/graph_utils/inc
                    ../ext/offload/inc
                    ../ext/offload/src
                    ../ext/parallel_cmd_utils/inc
                    ../ext/proxy/proxy_cmn/inc
                    ../ext/proxy/proxy_vcpm/inc
                    ../ext/pwr_mgr/inc
                    ../ext/runtime_link_hdlr/inc
                    ../ext/set_get_cfg/inc
                    ../ext/shared_mem/inc
                    ../ext/spf_cmd_hdlr/inc
                    ../ext/sys_util/inc
                    ../../amdb/ext/inc
                    ../ext/multi_client/inc
                    ../ext/shared_mem/src
                   )

#Add the sub directories
add_subdirectory(../core/build core)
add_subdirectory(../ext/build ext)
