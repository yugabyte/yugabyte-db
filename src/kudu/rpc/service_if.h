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
#ifndef KUDU_RPC_SERVICE_IF_H
#define KUDU_RPC_SERVICE_IF_H

#include <string>

#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/metrics.h"
#include "kudu/util/net/sockaddr.h"

namespace google {
namespace protobuf {
class Message;
}
}

namespace kudu {

class Histogram;

namespace rpc {

class InboundCall;

struct RpcMethodMetrics {
  RpcMethodMetrics();
  ~RpcMethodMetrics();

  scoped_refptr<Histogram> handler_latency;
};

// Handles incoming messages that initiate an RPC.
class ServiceIf {
 public:
  virtual ~ServiceIf();
  virtual void Handle(InboundCall* incoming) = 0;
  virtual void Shutdown();
  virtual std::string service_name() const = 0;

 protected:
  bool ParseParam(InboundCall* call, google::protobuf::Message* message);
  void RespondBadMethod(InboundCall* call);

};

} // namespace rpc
} // namespace kudu
#endif
