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

#include "yb/gutil/macros.h"
#include "yb/server/server_base.service.h"

namespace yb {
namespace server {

class RpcServerBase;

class GenericServiceImpl : public GenericServiceIf {
 public:
  explicit GenericServiceImpl(RpcServerBase* server);
  virtual ~GenericServiceImpl();

  void SetFlag(const SetFlagRequestPB* req,
               SetFlagResponsePB* resp,
               rpc::RpcContext rpc) override;

  void RefreshFlags(const RefreshFlagsRequestPB* req,
                    RefreshFlagsResponsePB* resp,
                    rpc::RpcContext rpc) override;

  void GetFlag(const GetFlagRequestPB* req,
               GetFlagResponsePB* resp,
               rpc::RpcContext rpc) override;

  void ValidateFlagValue(
      const ValidateFlagValueRequestPB* req, ValidateFlagValueResponsePB* resp,
      rpc::RpcContext rpc) override;

  void GetAutoFlagsConfigVersion(
      const GetAutoFlagsConfigVersionRequestPB* req,
      GetAutoFlagsConfigVersionResponsePB* resp,
      rpc::RpcContext rpc) override;

  void FlushCoverage(const FlushCoverageRequestPB* req,
                     FlushCoverageResponsePB* resp,
                     rpc::RpcContext rpc) override;

  void ServerClock(const ServerClockRequestPB* req,
                   ServerClockResponsePB* resp,
                   rpc::RpcContext rpc) override;

  void GetStatus(const GetStatusRequestPB* req,
                 GetStatusResponsePB* resp,
                 rpc::RpcContext rpc) override;

  void Ping(const PingRequestPB* req, PingResponsePB* resp, rpc::RpcContext rpc) override;

  // Reload TLS certificates to start using newly added certificates, if any.
  void ReloadCertificates(const ReloadCertificatesRequestPB* req,
                          ReloadCertificatesResponsePB* resp,
                          rpc::RpcContext context) override;

 private:
  RpcServerBase* server_;

  DISALLOW_COPY_AND_ASSIGN(GenericServiceImpl);
};

} // namespace server
} // namespace yb
