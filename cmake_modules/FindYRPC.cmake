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
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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
# RPC generator support
#########
find_package(Protobuf REQUIRED)

macro(YRPC_PROCESS_FILE FIL MESSAGES)
  get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
  get_filename_component(FIL_WE ${FIL} NAME_WE)

  # Ensure that the protobuf file is within the source root.
  # This is a requirement of protoc.
  FILE(RELATIVE_PATH PROTO_REL_TO_ROOT "${ARG_SOURCE_ROOT}" "${ABS_FIL}")
  FILE(RELATIVE_PATH PROTO_REL_TO_YB_SRC_ROOT "${YB_SRC_ROOT}" "${ABS_FIL}")

  GET_FILENAME_COMPONENT(REL_DIR "${PROTO_REL_TO_ROOT}" PATH)

  if(NOT REL_DIR STREQUAL "")
    SET(REL_DIR "${REL_DIR}/")
  endif()

  # For all the header files generated here, make sure they are also checked for in the
  # validate_proto_deps function in dep_graph_common.py. This will make sure that we don't add
  # depenedencies on these header files without also adding a dependency on a target that will
  # generate them. A failure to do so may result in non-deterministic build failures.
  set(PROTO_CC_OUT "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.pb.cc")
  set(PROTO_H_OUT "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.pb.h")
  set(SERVICE_CC "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.service.cc")
  set(SERVICE_H "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.service.h")
  set(PROXY_CC "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.proxy.cc")
  set(PROXY_H "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.proxy.h")
  set(FORWARD_H "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.fwd.h")
  set(MESSAGES_CC "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.messages.cc")
  set(MESSAGES_H "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.messages.h")
  list(APPEND ${SRCS} "${PROTO_CC_OUT}")
  list(APPEND ${HDRS} "${PROTO_H_OUT}" "${FORWARD_H}")
  set(OUTPUT_FILES "")
  list(APPEND OUTPUT_FILES "${PROTO_CC_OUT}" "${PROTO_H_OUT}" "${FORWARD_H}")

  if(${ARG_SERVICE})
    list(APPEND ${SRCS} "${SERVICE_CC}" "${PROXY_CC}")
    list(APPEND ${HDRS} "${SERVICE_H}" "${PROXY_H}")
    list(APPEND OUTPUT_FILES "${SERVICE_CC}" "${SERVICE_H}" "${PROXY_CC}" "${PROXY_H}")
  endif()

  set(YRPC_OPTS "")
  if (${MESSAGES})
    list(APPEND ${SRCS} "${MESSAGES_CC}")
    list(APPEND ${HDRS} "${MESSAGES_H}")
    list(APPEND OUTPUT_FILES "${MESSAGES_CC}" "${MESSAGES_H}")
    list(APPEND YRPC_OPTS "messages")
  endif()

  set(ARGS
      --plugin $<TARGET_FILE:protoc-gen-yrpc>
      --plugin $<TARGET_FILE:protoc-gen-insertions>
      --cpp_out ${ARG_BINARY_ROOT}
      --yrpc_out ${ARG_BINARY_ROOT}
      --insertions_out ${ARG_BINARY_ROOT}
      --proto_path ${ARG_SOURCE_ROOT}
      # Used to find built-in .proto files (e.g. FileDescriptorProto)
      --proto_path ${PROTOBUF_INCLUDE_DIR})

  if (YRPC_OPTS)
    list(APPEND ARGS --yrpc_opt ${YRPC_OPTS})
  endif ()

  list(APPEND ARGS ${EXTRA_PROTO_PATH_ARGS} ${ABS_FIL})

  add_custom_command(
    OUTPUT ${OUTPUT_FILES}
    COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
    ${ARGS}
    DEPENDS ${ABS_FIL} protoc-gen-yrpc protoc-gen-insertions
    COMMENT "Running C++ protocol buffer compiler with YRPC plugin on ${FIL}"
    VERBATIM)

  GET_PROTOBUF_GENERATION_TARGET_NAME("${PROTO_REL_TO_YB_SRC_ROOT}" TGT_NAME)
  set(TGT_DEPS "${PROTO_CC_OUT}" "${PROTO_H_OUT}" "${FORWARD_H}")
  if(${ARG_SERVICE})
    list(APPEND TGT_DEPS
      "${SERVICE_CC}" "${SERVICE_H}" protoc-gen-insertions
      "${PROXY_CC}" "${PROXY_H}")
  endif()
  if(${MESSAGES})
    list(APPEND TGT_DEPS "${MESSAGES_CC}" "${MESSAGES_H}")
  endif()
  add_custom_target(${TGT_NAME} DEPENDS ${TGT_DEPS})
  add_dependencies(gen_proto ${TGT_NAME})
  # This might be unnecessary, but we've seen issues with plugins not having been built in time.
  add_dependencies("${TGT_NAME}" protoc-gen-insertions protoc-gen-yrpc)
  list(APPEND ${TGTS} "${TGT_NAME}")
endmacro()
#
# Generate the YRPC files for a given .proto file.
#
function(YRPC_GENERATE SRCS HDRS TGTS)
  if(NOT ARGN)
    message(SEND_ERROR "Error: YRPC_GENERATE() called without protobuf files")
    return()
  endif(NOT ARGN)

  set(options)
  set(one_value_args SOURCE_ROOT BINARY_ROOT MESSAGES SERVICE)
  set(multi_value_args
      EXTRA_PROTO_PATHS PROTO_FILES MESSAGES_PROTO_FILES NO_SERVICE_PROTO_FILES
      NO_SERVICE_MESSAGES_PROTO_FILES)
  cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS)
    message(SEND_ERROR "Error: unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  if("${ARG_SERVICE}" STREQUAL "")
    set(ARG_SERVICE TRUE)
  endif()
  set(${SRCS})
  set(${HDRS})
  set(${TGTS})

  set(EXTRA_PROTO_PATH_ARGS)
  foreach(PP ${ARG_EXTRA_PROTO_PATHS})
    set(EXTRA_PROTO_PATH_ARGS ${EXTRA_PROTO_PATH_ARGS} --proto_path ${PP})
  endforeach()

  if("${ARG_SOURCE_ROOT}" STREQUAL "")
    SET(ARG_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  GET_FILENAME_COMPONENT(ARG_SOURCE_ROOT ${ARG_SOURCE_ROOT} ABSOLUTE)

  if("${ARG_BINARY_ROOT}" STREQUAL "")
    SET(ARG_BINARY_ROOT "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  GET_FILENAME_COMPONENT(ARG_BINARY_ROOT ${ARG_BINARY_ROOT} ABSOLUTE)

  foreach(FIL ${ARG_PROTO_FILES})
    YRPC_PROCESS_FILE("${FIL}" "${ARG_MESSAGES}")
  endforeach()
  foreach(FIL ${ARG_MESSAGES_PROTO_FILES})
    YRPC_PROCESS_FILE("${FIL}" "TRUE")
  endforeach()
  set(ARG_SERVICE FALSE)
  foreach(FIL ${ARG_NO_SERVICE_PROTO_FILES})
    YRPC_PROCESS_FILE("${FIL}" "${ARG_MESSAGES}")
  endforeach()
  foreach(FIL ${ARG_NO_SERVICE_MESSAGES_PROTO_FILES})
    YRPC_PROCESS_FILE("${FIL}" "TRUE")
  endforeach()

  set_source_files_properties(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
  set(${SRCS} ${${SRCS}} PARENT_SCOPE)
  set(${HDRS} ${${HDRS}} PARENT_SCOPE)
  set(${TGTS} ${${TGTS}} PARENT_SCOPE)
endfunction()
