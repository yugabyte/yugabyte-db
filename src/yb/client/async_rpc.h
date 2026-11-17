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

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/range/iterator_range_core.hpp>
#include <boost/version.hpp>

#include "yb/ash/ash_fwd.h"

#include "yb/client/in_flight_op.h"
#include "yb/client/tablet_rpc.h"

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/opid.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/memory/arena_fwd.h"
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
  scoped_refptr<Counter> skip_intents_writes;
};

using InFlightOps = boost::iterator_range<std::vector<InFlightOp>::iterator>;

struct AsyncRpcData {
  BatcherPtr batcher;
  RemoteTablet* tablet = nullptr;
  bool allow_local_calls_in_curr_thread = false;
  bool need_consistent_read = false;
  bool skip_intents = false;
  // Read this RPC's ops at in_txn_limit instead of the transaction read time.
  bool read_at_in_txn_limit = false;
  // Serve this RPC's reads as snapshot reads even in a SERIALIZABLE transaction.
  bool force_snapshot_read = false;
  ThreadSafeArenaPtr arena;
  InFlightOps ops;
  bool need_metadata = false;
  bool use_async_write = false;
  OpIds pending_async_write_op_ids = {};
};

struct FlushExtraResult {
  // Latest hybrid time that was present on tserver during processing of this request.
  HybridTime propagated_hybrid_time;

  // When read time was not specified by client it will contain read time that servers used
  // to process this request.
  ReadHybridTime used_read_time;
};

class AsyncRpc;

// A call that carries the requests of several AsyncRpcs, see RpcCoalescer.
struct CoalescedCall {
  virtual ~CoalescedCall() = default;

  rpc::RpcController controller;
};

// Packs the first attempts of the Write and Read RPCs of one batcher flush that go to the same
// tablet server into one WriteBatch or ReadBatch call. Each AsyncRpc still selects its tablet
// server and handles its response, errors and retries on its own; only the transport is shared.
// An rpc that does not reach SendRpcToTserver before Flush, for example because it waits for a
// tablet lookup, is sent alone.
class RpcCoalescer {
 public:
  RpcCoalescer(bool coalesce_writes, bool coalesce_reads)
      : coalesce_writes_(coalesce_writes), coalesce_reads_(coalesce_reads) {}

  // Returns true if the coalescer took the rpc, which is then sent by Flush.
  bool Add(AsyncRpc* rpc, const RemoteTabletServer* tserver);

  // Sends the rpcs taken so far. Later Add calls return false.
  void Flush();

 private:
  struct Entry {
    const RemoteTabletServer* tserver;
    bool is_write;
    std::shared_ptr<AsyncRpc> rpc;
  };

  const bool coalesce_writes_;
  const bool coalesce_reads_;
  std::mutex mutex_;
  bool flushed_ = false;
  std::vector<Entry> entries_;
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

  const ash::WaitStateInfoPtr& wait_state() const { return wait_state_; }

  std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy() const { return ts_proxy_; }

  // Whether the call got a response. If not, the response must not be read: the handler of a timed
  // out local call could still be filling it.
  bool HasResponse() const;

  bool allow_local_calls_in_curr_thread() const {
    return retrier().controller().allow_local_calls_in_curr_thread();
  }

  // The first attempt of this rpc goes through coalescer.
  void set_coalescer(std::shared_ptr<RpcCoalescer> coalescer) {
    coalescer_ = std::move(coalescer);
  }

  virtual void CallRemoteMethod() = 0;

 protected:
  friend class RpcCoalescer;

  // The sidecars of the response, which come either from this rpc's own call or from its part of
  // a coalesced call.
  Result<RefCntSlice> ExtractResponseSidecar(size_t idx) const;
  size_t TransferResponseSidecars(rpc::Sidecars* dest);
  RefCntBuffer ResponseDataHolder() const;

  // Set while this rpc's response comes from a coalesced call.
  std::shared_ptr<CoalescedCall> coalesced_call_;
  size_t coalesced_sidecar_offset_ = 0;
  size_t coalesced_sidecar_count_ = 0;
  std::shared_ptr<RpcCoalescer> coalescer_;

  void Finished(const Status& status) override;

  void HandleFinished(RefCntBuffer data_holder, const Status& status);

  void SendRpcToTserver(int attempt_num) override;

  // This is the last step where errors and responses are collected from the response and
  // stored in batcher. If there's a callback from the user, it is done in this step.
  virtual void ProcessResponseFromTserver(RefCntBuffer data_holder, const Status& status) = 0;

  // See FlushExtraResult for details.
  virtual FlushExtraResult MakeFlushExtraResult() = 0;

  virtual Status SwapResponses(RefCntBuffer data_holder) = 0;

  void Failed(const Status& status) override;

  // Is this a local call?
  bool IsLocalCall() const;

  rpc::RpcController* PrepareController() override;

  Status CheckResponseCount(
      const char* op, const char* name, size_t found, size_t expected);

  Status CheckResponseCount(
      const char* op, size_t redis_found, size_t redis_expected, size_t ql_found,
      size_t ql_expected, size_t pgsql_found, size_t pgsql_expected);

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

  std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy_;

  // Wait state captured at AsyncRpc construction
  ash::WaitStateInfoPtr wait_state_;
};

template <class Req, class Resp>
class AsyncRpcBase : public AsyncRpc {
 public:
  AsyncRpcBase(const AsyncRpcData& data, YBConsistencyLevel consistency_level);
  ~AsyncRpcBase();

  const Resp& resp() const { return resp_; }
  Resp& resp() { return resp_; }

  Req& req() { return req_; }

  // Completes this rpc with its part of a coalesced call. sub_resp is null when the call failed.
  void FinishCoalesced(
      std::shared_ptr<CoalescedCall> call, const Resp* sub_resp, size_t sidecar_offset,
      size_t sidecar_count, const Status& status);

  bool RefreshMetaCacheWithResponse() override;

  void SetRequestRaftConfigOpidIndex(int64_t opid_index) override;

  virtual std::string GetRpcName() = 0;

 protected:
  // Returns `true` if caller should continue processing response, `false` otherwise.
  bool CommonResponseCheck(const Status& status);
  void SendRpcToTserver(int attempt_num) override;

  virtual void NotifyBatcher(const Status& status) = 0;

  void ProcessResponseFromTserver(RefCntBuffer data_holder, const Status& status) override;

 protected: // TODO replace with private
  TabletServerErrorPtr response_error() const override;

  FlushExtraResult MakeFlushExtraResult() override;

  Req& req_;
  Resp& resp_;
};

class WriteRpc : public AsyncRpcBase<tserver::LWWriteRequestPB, tserver::LWWriteResponsePB> {
 public:
  // Relies on ops requests to be not on arena.
  explicit WriteRpc(const AsyncRpcData& data, rpc::ThreadPoolTag pool_tag = 0);

  virtual ~WriteRpc();

  std::string GetRpcName() override { return "Write"; }

  void CallRemoteMethod() override;

 private:
  Status SwapResponses(RefCntBuffer data_holder) override;
  void NotifyBatcher(const Status& status) override;
};

class ReadRpc : public AsyncRpcBase<tserver::LWReadRequestPB, tserver::LWReadResponsePB> {
 public:
  // Relies on ops requests to be not on arena.
  ReadRpc(
      const AsyncRpcData& data, YBConsistencyLevel yb_consistency_level,
      rpc::ThreadPoolTag pool_tag = 0);

  virtual ~ReadRpc();

  std::string GetRpcName() override { return "Read"; }

  void CallRemoteMethod() override;

 private:
  Status SwapResponses(RefCntBuffer data_holder) override;
  void NotifyBatcher(const Status& status) override;
};

// Called once per async write wait, with the tablet the write was sent to.
using AsyncWriteCompletionCallback =
    std::function<void(const TabletId& tracking_tablet_id, const OpId& op_id, const Status&)>;

// An async write whose completion the transaction still has to verify.
struct AsyncWriteWait {
  TabletId tracking_tablet_id;
  PartitionKey partition_key;
  std::shared_ptr<const YBTable> table;
  OpId op_id;
  // The tablet the write went to, used to find the leader when waits are batched.
  RemoteTabletPtr tablet;
  ash::WaitStateInfoPtr wait_state;
};

class WaitForAsyncWriteRpc : public rpc::Rpc, public TabletRpc {
 public:
  // Uses partition_key to handle potential tablet splits. tracking_tablet_id always points to the
  // original parent for tracking purposes.
  WaitForAsyncWriteRpc(
      YBClient* client, CoarseTimePoint deadline, TransactionId transaction_id,
      TabletId tracking_tablet_id, PartitionKey partition_key,
      const std::shared_ptr<const YBTable>& table, const OpId& op_id,
      const ash::WaitStateInfoPtr& issuing_wait_state, AsyncWriteCompletionCallback callback);

  WaitForAsyncWriteRpc(
      YBClient* client, CoarseTimePoint deadline, TransactionId transaction_id,
      const AsyncWriteWait& wait, AsyncWriteCompletionCallback callback)
      : WaitForAsyncWriteRpc(
            client, deadline, transaction_id, wait.tracking_tablet_id, wait.partition_key,
            wait.table, wait.op_id, wait.wait_state, std::move(callback)) {}

  ~WaitForAsyncWriteRpc() = default;

  void SendRpc() override;
  std::string ToString() const override;

 protected:
  TabletServerErrorPtr response_error() const override {
    return TabletServerErrorPtr(resp_.has_error() ? &resp_.error() : nullptr);
  }

  void SendRpcToTserver(int attempt_num) override;

  void Finished(const Status& status) override;

  void Failed(const Status& status) override;

 private:
  void OnKeyLookup(const Result<internal::RemoteTabletPtr>& result);
  void FinishOrRetry(Status&& status, bool allow_retry = true);

  YBClient* const client_;
  const TransactionId transaction_id_;
  const TabletId tracking_tablet_id_;
  const PartitionKey partition_key_;
  const OpId op_id_;
  const AsyncWriteCompletionCallback callback_;
  const std::shared_ptr<const YBTable> table_;
  TabletInvoker tablet_invoker_;
  tserver::WaitForAsyncWriteRequestPB req_;
  tserver::WaitForAsyncWriteResponsePB resp_;

  // Owns a copy of the issuing statement's ASH metadata, so we can adopt it when we send this RPC.
  ash::WaitStateInfoPtr wait_state_;

  rpc::RpcCommandPtr retained_self_;
};

// Verifies the given async writes, all led by one tablet server, with one WaitForAsyncWrites RPC.
// A write that the batch does not verify is waited for alone with WaitForAsyncWriteRpc, which
// handles leader changes and tablet splits.
void SendWaitForAsyncWrites(
    YBClient* client, RemoteTabletServer* tserver, CoarseTimePoint deadline,
    TransactionId transaction_id, std::vector<AsyncWriteWait> waits,
    AsyncWriteCompletionCallback callback);

}  // namespace internal
}  // namespace client
}  // namespace yb
