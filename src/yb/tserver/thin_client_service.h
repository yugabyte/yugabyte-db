// Copyright (c) YugabyteDB, Inc.
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

#include <future>
#include <memory>

#include "yb/client/client_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/thin_client.service.h"

namespace yb {

class ClockBase;

namespace tserver {

class PgMutationCounter;

// Serves thin Perform-based clients (see src/yb/thin_client): session heartbeat, table open and
// a Perform limited to batches of plain (non-transactional) reads or writes. Unlike
// PgClientService, which is versioned together with its only client (pggate), this service is
// used by clients that are upgraded independently of the cluster, so its implementation is kept
// separate and its behavior must stay backward compatible.
class ThinClientServiceImpl : public ThinClientServiceIf {
 public:
  ThinClientServiceImpl(
      const std::shared_future<client::YBClient*>& client_future,
      const scoped_refptr<ClockBase>& clock,
      const scoped_refptr<MetricEntity>& entity, rpc::Messenger* messenger,
      PgMutationCounter* pg_node_level_mutation_counter = nullptr);

  ~ThinClientServiceImpl();

  void Heartbeat(
      const ThinHeartbeatRequestPB* req, ThinHeartbeatResponsePB* resp,
      rpc::RpcContext context) override;

  void OpenTable(
      const ThinOpenTableRequestPB* req, ThinOpenTableResponsePB* resp,
      rpc::RpcContext context) override;

  void Perform(
      const LWThinPerformRequestPB* req, LWThinPerformResponsePB* resp,
      rpc::RpcContext context) override;

  void Shutdown() override;

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace tserver
}  // namespace yb
