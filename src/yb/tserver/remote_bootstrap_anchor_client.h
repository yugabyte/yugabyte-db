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

#include <gtest/gtest_prod.h>

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/opid_util.h"

#include "yb/gutil/macros.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/remote_bootstrap.pb.h"
#include "yb/tserver/remote_bootstrap.proxy.h"

#include "yb/util/status_fwd.h"

namespace yb {

class HostPort;

namespace tserver {

using SetLogAnchorRefreshStatusFunc = void(std::shared_ptr<rpc::RpcController> controller,
    const std::shared_ptr<UpdateLogAnchorResponsePB>&,
    const std::shared_ptr<KeepLogAnchorAliveResponsePB>&);

class RemoteBootstrapAnchorClient : public RefCountedThreadSafe<RemoteBootstrapAnchorClient> {
 public:
  RemoteBootstrapAnchorClient(
      const std::string& rbs_client_uuid,
      const std::string& owner_info,
      rpc::ProxyCache* proxy_cache,
      const HostPort& tablet_leader_peer_addr);

  Status RegisterLogAnchor(const std::string& tablet_id, const int64_t& log_index);

  Status UpdateLogAnchorAsync(const int64_t& log_index);

  Status UnregisterLogAnchor();

  Status KeepLogAnchorAliveAsync();

  Status ChangePeerRole();

  void SetLogAnchorRefreshStatus(
      std::shared_ptr<rpc::RpcController> controller,
      const std::shared_ptr<UpdateLogAnchorResponsePB> &update_anchor_resp,
      const std::shared_ptr<KeepLogAnchorAliveResponsePB> &keep_anchor_alive_resp);

  Status ProcessLogAnchorRefreshStatus();

  ~RemoteBootstrapAnchorClient();

 private:
  std::shared_ptr<RemoteBootstrapServiceProxy> proxy_;

  const std::string tablet_leader_peer_uuid_;
  const std::string rbs_client_uuid_;
  const std::string owner_info_;

  mutable std::mutex log_anchor_status_mutex_;
  Status log_anchor_refresh_status_ GUARDED_BY(log_anchor_status_mutex_) = Status::OK();

  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapAnchorClient);
};

}  // namespace tserver
}  // namespace yb
