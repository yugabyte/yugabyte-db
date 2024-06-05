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

set(YB_PCH_PREFIX gen_yrpc)
set(YB_PCH_DEP_LIBS gutil yb_util)

### RPC generator tool
add_executable(protoc-gen-yrpc
    forward_generator.cc
    messages_generator.cc
    metric_descriptor.cc
    model.cc
    printer.cc
    protoc-gen-yrpc.cc
    proxy_generator.cc
    service_generator.cc
    substitutions.cc)
target_link_libraries(protoc-gen-yrpc
    ${YB_BASE_LIBS}
    protoc
    protobuf
    gutil
    yb_util
    rpc_base_proto)
