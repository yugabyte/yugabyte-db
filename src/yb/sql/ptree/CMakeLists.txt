# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

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
