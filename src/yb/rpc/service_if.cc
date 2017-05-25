// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "yb/rpc/service_if.h"

#include <string>

#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/inbound_call.h"
#include "yb/rpc/rpc_header.pb.h"

using std::string;
using strings::Substitute;

namespace yb {
namespace rpc {

RpcMethodMetrics::RpcMethodMetrics()
  : handler_latency(nullptr) {
}

RpcMethodMetrics::~RpcMethodMetrics() {
}

ServiceIf::~ServiceIf() {
}

void ServiceIf::Shutdown() {
}

bool ServiceIf::ParseParam(InboundCall *call, google::protobuf::Message *message) {
  Slice param(call->serialized_request());
  if (PREDICT_FALSE(!message->ParseFromArray(param.data(), param.size()))) {
    string err = Substitute("Invalid parameter for call $0: $1",
                            call->remote_method().ToString(),
                            message->InitializationErrorString().c_str());
    LOG(WARNING) << err;
    call->RespondFailure(ErrorStatusPB::ERROR_INVALID_REQUEST,
                         STATUS(InvalidArgument, err));
    return false;
  }
  return true;
}

void ServiceIf::RespondBadMethod(InboundCall *call) {
  auto err = Substitute("Call on service $0 received from $1 with an invalid method name: $2",
                        call->remote_method().service_name(),
                        call->connection()->ToString(),
                        call->remote_method().method_name());
  LOG(WARNING) << err;
  call->RespondFailure(ErrorStatusPB::ERROR_NO_SUCH_METHOD,
                       STATUS(InvalidArgument, err));
}

} // namespace rpc
} // namespace yb
