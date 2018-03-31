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

add_library(pgsql-ptree
            parse_tree.cc
            tree_node.cc
            pg_option.cc
            pg_tdatabase.cc
            pg_tcreate_schema.cc
            pg_tcreate_table.cc
            pg_tdml.cc
            pg_tselect.cc
            pg_tdelete.cc
            pg_tinsert.cc
            pg_tupdate.cc
            pg_tdrop.cc
            pg_ttype.cc
            pg_tname.cc
            pg_texpr.cc
            pg_tbcall.cc
            pg_process_context.cc
            pg_compile_context.cc
            pg_sem_state.cc)

target_link_libraries(pgsql-ptree pgsql-util yb_client yb_util yb_bfpg yb_common)
add_dependencies(pgsql-ptree yb_bfpg)
