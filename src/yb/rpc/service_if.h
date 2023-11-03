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
#pragma once

#include <string>

#include <boost/functional/hash.hpp>

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/remote_method.h"

#include "yb/util/metrics_fwd.h"

namespace google {
namespace protobuf {
class Message;
}
}

namespace yb {
namespace rpc {

struct RpcMethodMetrics {
  scoped_refptr<Counter> request_bytes;
  scoped_refptr<Counter> response_bytes;
  scoped_refptr<Histogram> handler_latency;

  RpcMethodMetrics();
  RpcMethodMetrics(const scoped_refptr<Counter>& request_bytes,
                   const scoped_refptr<Counter>& response_bytes,
                   const scoped_refptr<Histogram>& handler_latency);
  RpcMethodMetrics(const RpcMethodMetrics&);
  ~RpcMethodMetrics();
};

struct RpcMethodDesc {
  RemoteMethod method;
  std::function<void(InboundCallPtr)> handler;
  RpcMethodMetrics metrics;
};

// Handles incoming messages that initiate an RPC.
class ServiceIf {
 public:
  virtual ~ServiceIf();
  virtual void FillEndpoints(const RpcServicePtr& service, RpcEndpointMap* map) = 0;
  virtual void Handle(InboundCallPtr incoming) = 0;

  virtual void Shutdown();
  virtual std::string service_name() const = 0;
};

}  // namespace rpc
}  // namespace yb
