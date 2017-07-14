# Copyright (c) YugaByte, Inc.

add_library(ybsql_ptree
            parse_tree.cc
            tree_node.cc
            pt_type.cc
            pt_name.cc
            pt_property.cc
            pt_keyspace_property.cc
            pt_table_property.cc
            pt_create_keyspace.cc
            pt_use_keyspace.cc
            pt_create_table.cc
            pt_alter_table.cc
            pt_create_type.cc
            pt_drop.cc
            pt_dml.cc
            pt_select.cc
            pt_insert.cc
            pt_delete.cc
            pt_update.cc
            pt_expr.cc
            pt_bcall.cc
            process_context.cc
            sem_context.cc
            sem_state.cc)

target_link_libraries(ybsql_ptree ybsql_util yb_client yb_util yb_bfyql yb_common)
add_dependencies(ybsql_ptree yb_bfyql)
