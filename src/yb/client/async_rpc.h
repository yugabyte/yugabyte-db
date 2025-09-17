// Copyright (c) YugaByte, Inc.
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

#include <boost/range/iterator_range_core.hpp>
#include <boost/version.hpp>

#include "yb/client/in_flight_op.h"
#include "yb/client/tablet_rpc.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/opid.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/metrics_fwd.h"

namespace yb {
namespace client {

class YBTable;
class YBClient;

namespace internal {

class Batcher;
struct InFlightOp;
class RemoteTablet;
class RemoteTabletServer;

// Container for async rpc metrics
struct AsyncRpcMetrics {
  explicit AsyncRpcMetrics(const scoped_refptr<MetricEntity>& metric_entity);

  scoped_refptr<EventStats> remote_write_rpc_time;
  scoped_refptr<EventStats> remote_read_rpc_time;
  scoped_refptr<EventStats> local_write_rpc_time;
  scoped_refptr<EventStats> local_read_rpc_time;
  scoped_refptr<EventStats> time_to_send;
  scoped_refptr<Counter> consistent_prefix_successful_reads;
  scoped_refptr<Counter> consistent_prefix_failed_reads;
};

using InFlightOps = boost::iterator_range<std::vector<InFlightOp>::iterator>;

struct AsyncRpcData {
  BatcherPtr batcher;
  RemoteTablet* tablet = nullptr;
  bool allow_local_calls_in_curr_thread = false;
  bool need_consistent_read = false;
  InFlightOps ops;
  bool need_metadata = false;
  bool use_async_write = false;
};

struct FlushExtraResult {
  // Latest hybrid time that was present on tserver during processing of this request.
  HybridTime propagated_hybrid_time;

  // When read time was not specified by client it will contain read time that servers used
  // to process this request.
  ReadHybridTime used_read_time;
};

// An Async RPC which is in-flight to a tablet. Initially, the RPC is sent
// to the leader replica, but it may be retried with another replica if the
// leader fails.
//
// Keeps a reference on the owning batcher while alive. It doesn't take a generic callback,
// but ProcessResponseFromTserver will update the state after getting the end response.
// This class deletes itself after Rpc returns and is processed.
class AsyncRpc : public rpc::Rpc, public TabletRpc {
 public:
  AsyncRpc(const AsyncRpcData& data, YBConsistencyLevel consistency_level);

  virtual ~AsyncRpc();

  void SendRpc() override;
  std::string ToString() const override;

  std::shared_ptr<const YBTable> table() const;
  const RemoteTablet& tablet() const { return *tablet_invoker_.tablet(); }
  const InFlightOps& ops() const { return ops_; }

 protected:
  void Finished(const Status& status) override;

  void HandleFinished(const Status& status);

  void SendRpcToTserver(int attempt_num) override;

  virtual void CallRemoteMethod() = 0;

  // This is the last step where errors and responses are collected from the response and
  // stored in batcher. If there's a callback from the user, it is done in this step.
  virtual void ProcessResponseFromTserver(const Status& status) = 0;

  // See FlushExtraResult for details.
  virtual FlushExtraResult MakeFlushExtraResult() = 0;

  virtual Status SwapResponses() = 0;

  void Failed(const Status& status) override;

  // Is this a local call?
  bool IsLocalCall() const;

  Status CheckResponseCount(
      const char* op, const char* name, int found, int expected);

  Status CheckResponseCount(
      const char* op, int redis_found, int redis_expected, int ql_found, int ql_expected,
      int pgsql_found, int pgsql_expected);

  // Pointer back to the batcher. Processes the write response when it
  // completes, regardless of success or failure.
  BatcherPtr batcher_;

  // Operations which were batched into this RPC.
  // These operations are in kRequestSent state.
  InFlightOps ops_;

  TabletInvoker tablet_invoker_;

  CoarseTimePoint start_;
  std::shared_ptr<AsyncRpcMetrics> async_rpc_metrics_;
  rpc::RpcCommandPtr retained_self_;
};

template <class Req, class Resp>
class AsyncRpcBase : public AsyncRpc {
 public:
  AsyncRpcBase(const AsyncRpcData& data, YBConsistencyLevel consistency_level);
  ~AsyncRpcBase();

  const Resp& resp() const { return resp_; }
  Resp& resp() { return resp_; }

  bool RefreshMetaCacheWithResponse() override;

  void SetRequestRaftConfigOpidIndex(int64_t opid_index) override;

  virtual std::string GetRpcName() = 0;

 protected:
  // Returns `true` if caller should continue processing response, `false` otherwise.
  bool CommonResponseCheck(const Status& status);
  void SendRpcToTserver(int attempt_num) override;

  virtual void NotifyBatcher(const Status& status) = 0;

  void ProcessResponseFromTserver(const Status& status) override;

 protected: // TODO replace with private
  const tserver::TabletServerErrorPB* response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

  FlushExtraResult MakeFlushExtraResult() override;

  Req req_;
  Resp resp_;
};

class WriteRpc : public AsyncRpcBase<tserver::WriteRequestPB, tserver::WriteResponsePB> {
 public:
  // Relies on ops requests to be not on arena.
  explicit WriteRpc(const AsyncRpcData& data);

  virtual ~WriteRpc();

  std::string GetRpcName() override { return "Write"; }

  std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy() const { return ts_proxy_; }

 private:
  Status SwapResponses() override;
  void CallRemoteMethod() override;
  void NotifyBatcher(const Status& status) override;

  std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy_;
};

class ReadRpc : public AsyncRpcBase<tserver::ReadRequestPB, tserver::ReadResponsePB> {
 public:
  // Relies on ops requests to be not on arena.
  explicit ReadRpc(const AsyncRpcData& data, YBConsistencyLevel yb_consistency_level);

  virtual ~ReadRpc();

  std::string GetRpcName() override { return "Read"; }

 private:
  Status SwapResponses() override;
  void CallRemoteMethod() override;
  void NotifyBatcher(const Status& status) override;
};

class WaitForAsyncWriteRpc : public rpc::Rpc, public TabletRpc {
 public:
  WaitForAsyncWriteRpc(
      const BatcherPtr& batcher, const TabletId& tablet_id,
      std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy, const OpId& op_id);

  ~WaitForAsyncWriteRpc() = default;

  void SendRpc() override;
  std::string ToString() const override;

 protected:
  const tserver::TabletServerErrorPB* response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

  void SendRpcToTserver(int attempt_num) override;

  void Finished(const Status& status) override;

  void Failed(const Status& status) override;

 private:
  const TabletId tablet_id_;
  const OpId op_id_;
  BatcherPtr batcher_;
  std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy_;
  tserver::WaitForAsyncWriteRequestPB req_;
  tserver::WaitForAsyncWriteResponsePB resp_;

  rpc::RpcCommandPtr retained_self_;
};

}  // namespace internal
}  // namespace client
}  // namespace yb
