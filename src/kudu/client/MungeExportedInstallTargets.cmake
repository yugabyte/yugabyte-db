# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Finds all Kudu client cmake installation files and replaces all references
# to kudu_client_exported with kudu_client, thus renaming the targets.

set(CMAKE_FILES_DIR "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/kuduClient/cmake")
if(NOT EXISTS ${CMAKE_FILES_DIR})
  message(FATAL_ERROR "Cannot find cmake installation directory ${CMAKE_FILES_DIR}")
endif()
file(GLOB CMAKE_FILES "${CMAKE_FILES_DIR}/*.cmake")
foreach(CMAKE_FILE ${CMAKE_FILES})
  message(STATUS "Munging kudu client targets in ${CMAKE_FILE}")
  execute_process(COMMAND sed s/kudu_client_exported/kudu_client/g ${CMAKE_FILE}
    OUTPUT_FILE ${CMAKE_FILE}.new)
  execute_process(COMMAND mv -f ${CMAKE_FILE}.new ${CMAKE_FILE})
endforeach()
