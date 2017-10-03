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
# Kudu RPC generator support
#########
find_package(Protobuf REQUIRED)

#
# Generate the KRPC files for a given .proto file.
#
function(KRPC_GENERATE SRCS HDRS TGTS)
  if(NOT ARGN)
    message(SEND_ERROR "Error: KRPC_GENERATE() called without protobuf files")
    return()
  endif(NOT ARGN)

  set(options)
  set(one_value_args SOURCE_ROOT BINARY_ROOT)
  set(multi_value_args EXTRA_PROTO_PATHS PROTO_FILES)
  cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS)
    message(SEND_ERROR "Error: unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
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
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    get_filename_component(FIL_WE ${FIL} NAME_WE)

    # Ensure that the protobuf file is within the source root.
    # This is a requirement of protoc.
    FILE(RELATIVE_PATH PROTO_REL_TO_ROOT "${ARG_SOURCE_ROOT}" "${ABS_FIL}")

    GET_FILENAME_COMPONENT(REL_DIR "${PROTO_REL_TO_ROOT}" PATH)

    if(NOT REL_DIR STREQUAL "")
      SET(REL_DIR "${REL_DIR}/")
    endif()

    set(PROTO_CC_OUT "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.pb.cc")
    set(PROTO_H_OUT "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.pb.h")
    set(SERVICE_CC "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.service.cc")
    set(SERVICE_H "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.service.h")
    set(PROXY_CC "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.proxy.cc")
    set(PROXY_H "${ARG_BINARY_ROOT}/${REL_DIR}${FIL_WE}.proxy.h")
    list(APPEND ${SRCS} "${PROTO_CC_OUT}" "${SERVICE_CC}" "${PROXY_CC}")
    list(APPEND ${HDRS} "${PROTO_H_OUT}" "${SERVICE_H}" "${PROXY_H}")

    add_custom_command(
      OUTPUT "${SERVICE_CC}" "${SERVICE_H}" "${PROXY_CC}" "${PROXY_H}"
             "${PROTO_CC_OUT}" "${PROTO_H_OUT}"
      COMMAND  ${PROTOBUF_PROTOC_EXECUTABLE}
      ARGS --plugin $<TARGET_FILE:protoc-gen-krpc>
           --plugin $<TARGET_FILE:protoc-gen-insertions>
           --cpp_out ${ARG_BINARY_ROOT}
           --krpc_out ${ARG_BINARY_ROOT}
           --insertions_out ${ARG_BINARY_ROOT}
           --proto_path ${ARG_SOURCE_ROOT}
           # Used to find built-in .proto files (e.g. FileDescriptorProto)
           --proto_path ${PROTOBUF_INCLUDE_DIR}
           ${EXTRA_PROTO_PATH_ARGS} ${ABS_FIL}
      DEPENDS ${ABS_FIL} protoc-gen-krpc protoc-gen-insertions
      COMMENT "Running C++ protocol buffer compiler with KRPC plugin on ${FIL}"
      VERBATIM)

    # This custom target enforces that there's just one invocation of protoc
    # when there are multiple consumers of the generated files. The target name
    # must be unique; adding parts of the filename helps ensure this.
    set(TGT_NAME ${REL_DIR}${FIL})
    string(REPLACE "/" "-" TGT_NAME ${TGT_NAME})
    add_custom_target(${TGT_NAME}
      DEPENDS "${SERVICE_CC}" "${SERVICE_H}"
      "${PROXY_CC}" "${PROXY_H}"
      "${PROTO_CC_OUT}" "${PROTO_H_OUT}")
    list(APPEND ${TGTS} "${TGT_NAME}")
  endforeach()

  set_source_files_properties(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
  set(${SRCS} ${${SRCS}} PARENT_SCOPE)
  set(${HDRS} ${${HDRS}} PARENT_SCOPE)
  set(${TGTS} ${${TGTS}} PARENT_SCOPE)
endfunction()
