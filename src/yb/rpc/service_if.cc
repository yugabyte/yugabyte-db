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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#include "yb/rpc/service_if.h"

#include <string>

#include "yb/util/metrics.h"

using std::string;

namespace yb {
namespace rpc {

ServiceIf::~ServiceIf() {
}

void ServiceIf::Shutdown() {
}

RpcMethodMetrics::RpcMethodMetrics() = default;

RpcMethodMetrics::RpcMethodMetrics(const scoped_refptr<Counter>& request_bytes_,
                                   const scoped_refptr<Counter>& response_bytes_,
                                   const scoped_refptr<Histogram>& handler_latency_)
    : request_bytes(request_bytes_), response_bytes(response_bytes_),
      handler_latency(handler_latency_) {
}

RpcMethodMetrics::~RpcMethodMetrics() = default;

RpcMethodMetrics::RpcMethodMetrics(const RpcMethodMetrics&) = default;

} // namespace rpc
} // namespace yb
