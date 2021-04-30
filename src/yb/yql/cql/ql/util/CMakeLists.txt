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

add_library(ql_util
            cql_message.cc
            errcodes.cc
            statement_params.cc
            statement_result.cc
            ql_env.cc)

target_link_libraries(ql_util
                      yb_client
                      yb_util
                      lz4
                      snappy)
add_dependencies(ql_util ql_parser_flex_bison_output)
