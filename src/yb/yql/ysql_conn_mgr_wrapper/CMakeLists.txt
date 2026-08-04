# Copyright (c) YugabyteDB, Inc.
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

set(YSQL_CONN_MGR_WRAPPER_SRCS
    ysql_conn_mgr_wrapper.cc
    ysql_conn_mgr_conf.cc)

set(YSQL_CONN_MGR_WRAPPER_LIBS
    yb_util
    yb_process_wrapper)

ADD_YB_LIBRARY(ysql_conn_mgr_wrapper
               SRCS ${YSQL_CONN_MGR_WRAPPER_SRCS}
               DEPS ${YSQL_CONN_MGR_WRAPPER_LIBS})

add_custom_target(update_ysql_conn_mgr_template ALL
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/ysql_conn_mgr.template.conf
            ${YB_BUILD_ROOT}/share/ysql_conn_mgr.template.conf
    COMMAND ${CMAKE_COMMAND} -E touch ${YB_BUILD_ROOT}/share/ysql_conn_mgr.template.conf
    COMMENT "Copying the template file for the Ysql Connection Manager config."
    COMMAND_ERROR_IS_FATAL  # Treat command errors as fatal
)
