#[[
   @file CMakeLists.txt

   @brief
]]
cmake_minimum_required(VERSION 3.10)

#Add the sub directories
add_subdirectory(../apm_db_query/build apm_db_query)
add_subdirectory(../close_all/build close_all)
add_subdirectory(../cmn/build cmn)
add_subdirectory(../cntr_peer_heap_utils/build cntr_peer_heap_utils)
add_subdirectory(../data_path/build data_path)
add_subdirectory(../debug_info_cfg/build debug_info_cfg)
add_subdirectory(../debug_info_dump/build debug_info_dump)
add_subdirectory(../err_hdlr/build err_hdlr)
add_subdirectory(../gpr_cmd_rsp_hdlr/build gpr_cmd_rsp_hdlr)
add_subdirectory(../graph_utils/build graph_utils)
add_subdirectory(../offload/build offload)
add_subdirectory(../parallel_cmd_utils/build parallel_cmd_utils)
add_subdirectory(../proxy/build proxy)
add_subdirectory(../pwr_mgr/build pwr_mgr)
add_subdirectory(../runtime_link_hdlr/build runtime_link_hdlr)
add_subdirectory(../set_get_cfg/build set_get_cfg)
add_subdirectory(../shared_mem/build shared_mem)
add_subdirectory(../spf_cmd_hdlr/build spf_cmd_hdlr)
add_subdirectory(../sys_util/build sys_util)
add_subdirectory(../multi_client/build multi_client)
