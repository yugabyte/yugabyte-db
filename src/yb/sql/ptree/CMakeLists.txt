# Copyright (c) YugaByte, Inc.

add_library(ybsql_ptree
            parse_tree.cc
            tree_node.cc
            list_node.cc
            pt_type.cc
            pt_name.cc
            pt_create_table.cc
            process_context.cc
            sem_context.cc)

target_link_libraries(ybsql_ptree ybsql_util yb_util)
