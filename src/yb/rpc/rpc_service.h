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

#include "yb/rpc/rpc_fwd.h"

namespace yb {
namespace rpc {

class RpcService : public RefCountedThreadSafe<RpcService> {
 public:
  virtual ~RpcService() {}

  virtual void FillEndpoints(RpcEndpointMap* map) = 0;

  // Enqueue a call for processing.
  // On failure, the RpcService::QueueInboundCall() implementation is
  // responsible for responding to the client with a failure message.
  virtual void QueueInboundCall(InboundCallPtr call) = 0;

  // Handle a call directly.
  virtual void Handle(InboundCallPtr call) = 0;

  // Initiate RPC service shutdown.
  // Two phase shutdown is required to prevent shutdown deadlock of 2 dependent resources.
  virtual void StartShutdown() = 0;

  // Wait until RPC sevrice shutdown completes.
  virtual void CompleteShutdown() = 0;

  void Shutdown() {
    StartShutdown();
    CompleteShutdown();
  }
};

} // namespace rpc
} // namespace yb
