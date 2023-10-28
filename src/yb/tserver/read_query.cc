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

#include "yb/tserver/read_query.h"

#include "yb/common/row_mark.h"
#include "yb/common/transaction.h"

#include "yb/gutil/bind.h"

#include "yb/rpc/sidecars.h"

#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/read_result.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/write_query.h"

#include "yb/tserver/service_util.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver.pb.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/trace.h"

using namespace std::literals;

DEFINE_test_flag(int32, transactional_read_delay_ms, 0,
                 "Amount of time to delay between transaction status check and reading start.");

DEFINE_test_flag(int32, simulate_time_out_failures_msecs, 0, "If greater than 0, we will randomly "
                 "mark read requests as timed out and sleep for the specificed amount of time by "
                 "this flag to simulate time out failures. The requester will mark the timed out "
                 "replica as failed, and its periodic refresh mechanism for the lookup cache will "
                 "mark them as available.");

DEFINE_test_flag(bool, assert_reads_served_by_follower, false, "If set, we verify that the "
                 "consistency level is CONSISTENT_PREFIX, and that this server is not the leader "
                 "for the tablet");

DEFINE_RUNTIME_bool(parallelize_read_ops, true,
    "Controls whether multiple (Redis) read ops that are present in a operation "
    "should be executed in parallel.");
TAG_FLAG(parallelize_read_ops, advanced);

DEFINE_RUNTIME_bool(ysql_follower_reads_avoid_waiting_for_safe_time, true,
    "Controls whether ysql follower reads that specify a not-yet-safe read time "
    "should be rejected. This will force them to go to the leader, which will likely be "
    "faster than waiting for safe time to catch up.");
TAG_FLAG(ysql_follower_reads_avoid_waiting_for_safe_time, advanced);

namespace yb {
namespace tserver {

namespace {

void HandleRedisReadRequestAsync(
    tablet::AbstractTablet* tablet,
    const docdb::ReadOperationData& read_operation_data,
    const RedisReadRequestPB& redis_read_request,
    RedisResponsePB* response,
    const std::function<void(const Status& s)>& status_cb
) {
  status_cb(tablet->HandleRedisReadRequest(read_operation_data, redis_read_request, response));
}

class ReadQuery : public std::enable_shared_from_this<ReadQuery>, public rpc::ThreadPoolTask {
 public:
  ReadQuery(
      TabletServerIf* server, ReadTabletProvider* read_tablet_provider, const ReadRequestPB* req,
      ReadResponsePB* resp, rpc::RpcContext context)
      : server_(*server), read_tablet_provider_(*read_tablet_provider), req_(req), resp_(resp),
        context_(std::move(context)) {}

  void Perform() {
    RespondIfFailed(DoPerform());
  }

  void RespondIfFailed(const Status& status) {
    if (!status.ok()) {
      RespondFailure(status);
    }
  }

  void RespondFailure(const Status& status) {
    SetupErrorAndRespond(resp_->mutable_error(), status, &context_);
  }

  virtual ~ReadQuery() = default;

 private:
  Status DoPerform();

  // Picks read based for specified read context.
  Status DoPickReadTime(server::Clock* clock);

  bool transactional() const;

  tablet::Tablet* tablet() const;

  ReadHybridTime FormRestartReadHybridTime(const HybridTime& restart_time) const;

  Status PickReadTime(server::Clock* clock);

  bool IsForBackfill() const;
  bool IsPgsqlFollowerReadAtAFollower() const;

  // Read implementation. If restart is required returns restart time, in case of success
  // returns invalid ReadHybridTime. Otherwise returns error status.
  Result<ReadHybridTime> DoRead();
  Result<ReadHybridTime> DoReadImpl();

  Status Complete();

  void UpdateConsistentPrefixMetrics();

  // Used when we write intents during read, i.e. for serializable isolation.
  // We cannot proceed with read from completion callback, to avoid holding
  // replica state lock for too long.
  // So ThreadPool is used to proceed with read.
  void Run() override {
    auto status = PickReadTime(server_.Clock());
    if (status.ok()) {
      status = Complete();
    }
    RespondIfFailed(status);
  }

  void Done(const Status& status) override {
    RespondIfFailed(status);
    retained_self_ = nullptr;
  }

  TabletServerIf& server_;
  ReadTabletProvider& read_tablet_provider_;
  const ReadRequestPB* req_;
  ReadResponsePB* resp_;
  rpc::RpcContext context_;

  std::shared_ptr<tablet::AbstractTablet> abstract_tablet_;

  ReadHybridTime read_time_;
  HybridTime safe_ht_to_read_;
  ReadHybridTime used_read_time_;
  tablet::RequireLease require_lease_ = tablet::RequireLease::kFalse;
  HostPortPB host_port_pb_;
  bool allow_retry_ = false;
  bool reading_from_non_leader_ = false;
  RequestScope request_scope_;
  std::shared_ptr<ReadQuery> retained_self_;
};

bool ReadQuery::transactional() const {
  return abstract_tablet_->IsTransactionalRequest(req_->pgsql_batch_size() > 0);
}

tablet::Tablet* ReadQuery::tablet() const {
  return down_cast<tablet::Tablet*>(abstract_tablet_.get());
}

ReadHybridTime ReadQuery::FormRestartReadHybridTime(const HybridTime& restart_time) const {
  DCHECK_GT(restart_time, read_time_.read);
  VLOG(1) << "Restart read required at: " << restart_time << ", original: " << read_time_;
  auto result = read_time_;
  result.read = std::min(std::max(restart_time, safe_ht_to_read_), read_time_.global_limit);
  result.local_limit = std::min(safe_ht_to_read_, read_time_.global_limit);
  return result;
}

Status ReadQuery::PickReadTime(server::Clock* clock) {
  auto result = DoPickReadTime(clock);
  if (!result.ok()) {
    TRACE(result.ToString());
  }
  return result;
}

bool ReadQuery::IsForBackfill() const {
  if (req_->pgsql_batch_size() > 0) {
    if (req_->pgsql_batch(0).is_for_backfill()) {
      // Currently, read requests for backfill should only come by themselves, not in batches.
      DCHECK_EQ(req_->pgsql_batch_size(), 1);
      return true;
    }
  }
  // YCQL doesn't send read RPCs for scanning the indexed table and instead directly reads using
  // iterator, so there's no equivalent logic for YCQL here.
  return false;
}

Status ReadQuery::DoPerform() {
  if (req_->include_trace()) {
    context_.EnsureTraceCreated();
  }
  ADOPT_TRACE(context_.trace());
  TRACE("Start Read");
  TRACE_EVENT1("tserver", "TabletServiceImpl::Read", "tablet_id", req_->tablet_id());
  VLOG(2) << "Received Read RPC: " << req_->DebugString();
  // Unfortunately, determining the isolation level is not as straightforward as it seems. All but
  // the first request to a given tablet by a particular transaction assume that the tablet already
  // has the transaction metadata, including the isolation level, and those requests expect us to
  // retrieve the isolation level from that metadata. Failure to do so was the cause of a
  // serialization anomaly tested by TestOneOrTwoAdmins
  // (https://github.com/yugabyte/yugabyte-db/issues/1572).

  bool serializable_isolation = false;
  TabletPeerTablet peer_tablet;
  if (req_->has_transaction()) {
    IsolationLevel isolation_level;
    if (req_->transaction().has_isolation()) {
      // This must be the first request to this tablet by this particular transaction.
      isolation_level = req_->transaction().isolation();
    } else {
      peer_tablet = VERIFY_RESULT(LookupTabletPeer(
          server_.tablet_peer_lookup(), req_->tablet_id()));
      isolation_level = VERIFY_RESULT(peer_tablet.tablet->GetIsolationLevelFromPB(*req_));
    }
    serializable_isolation = isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION;

    if (PREDICT_FALSE(FLAGS_TEST_transactional_read_delay_ms > 0)) {
      LOG(INFO) << "Delaying transactional read for "
                << FLAGS_TEST_transactional_read_delay_ms << " ms.";
      SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_transactional_read_delay_ms));
    }

#if defined(DUMP_READ)
    if (req->pgsql_batch().size() > 0) {
      LOG(INFO) << CHECK_RESULT(FullyDecodeTransactionId(req->transaction().transaction_id()))
                << " READ: " << req->pgsql_batch(0).partition_column_values(0).value().int32_value()
                << ", " << isolation_level;
    }
#endif
  }

  // Get the most restrictive row mark present in the batch of PostgreSQL requests.
  // TODO: rather handle individual row marks once we start batching read requests (issue #2495)
  RowMarkType batch_row_mark = RowMarkType::ROW_MARK_ABSENT;
  CatalogVersionChecker catalog_version_checker(server_);
  for (const auto& pg_req : req_->pgsql_batch()) {
    RETURN_NOT_OK(catalog_version_checker(pg_req));
    RowMarkType current_row_mark = GetRowMarkTypeFromPB(pg_req);
    if (IsValidRowMarkType(current_row_mark)) {
      if (!req_->has_transaction()) {
        return STATUS(
            NotSupported, "Read request with row mark types must be part of a transaction",
            TabletServerError(TabletServerErrorPB::OPERATION_NOT_SUPPORTED));
      }
      batch_row_mark = GetStrongestRowMarkType({current_row_mark, batch_row_mark});
    }
  }
  const bool has_row_mark = IsValidRowMarkType(batch_row_mark);

  LeaderTabletPeer leader_peer;
  auto tablet_peer = peer_tablet.tablet_peer;

  if (serializable_isolation || has_row_mark) {
    // At this point we expect that we don't have pure read serializable transactions, and
    // always write read intents to detect conflicts with other writes.
    leader_peer = VERIFY_RESULT(LookupLeaderTablet(
        server_.tablet_peer_lookup(), req_->tablet_id(), std::move(peer_tablet)));
    // Serializable read adds intents, i.e. writes data.
    // We should check for memory pressure in this case.
    RETURN_NOT_OK(CheckWriteThrottling(req_->rejection_score(), leader_peer.peer.get()));
    abstract_tablet_ = VERIFY_RESULT(leader_peer.peer->shared_tablet_safe());
  } else {
    abstract_tablet_ = VERIFY_RESULT(read_tablet_provider_.GetTabletForRead(
        req_->tablet_id(), std::move(peer_tablet.tablet_peer),
        req_->consistency_level(), AllowSplitTablet::kFalse));
    leader_peer.leader_term = OpId::kUnknownTerm;
  }

  if (req_->consistency_level() == YBConsistencyLevel::CONSISTENT_PREFIX) {
    if (abstract_tablet_) {
      tablet()->metrics()->Increment(
          tablet::TabletCounters::kConsistentPrefixReadRequests);
    }
  }

  // For virtual tables held at master the tablet peer may not be found.
  if (!tablet_peer) {
    tablet_peer = ResultToValue(
        server_.tablet_peer_lookup()->GetServingTablet(req_->tablet_id()), {});
  }
  reading_from_non_leader_ = tablet_peer && !CheckPeerIsLeader(*tablet_peer).ok();
  if (PREDICT_FALSE(FLAGS_TEST_assert_reads_served_by_follower)) {
    CHECK_NE(req_->consistency_level(), YBConsistencyLevel::STRONG)
        << "--TEST_assert_reads_served_by_follower is true but consistency level is "
           "invalid: YBConsistencyLevel::STRONG";
    CHECK(reading_from_non_leader_)
        << "--TEST_assert_reads_served_by_follower is true but read is being served by "
        << " peer " << tablet_peer->permanent_uuid() << " which is the leader for tablet "
        << req_->tablet_id();
  }

  if (!abstract_tablet_->system() && tablet()->metadata()->hidden()) {
    return STATUS(
        NotFound, "Tablet not found", req_->tablet_id(),
        TabletServerError(TabletServerErrorPB::TABLET_NOT_FOUND));
  }

  if (FLAGS_TEST_simulate_time_out_failures_msecs > 0 && RandomUniformInt(0, 10) < 2) {
    LOG(INFO) << "Marking request as timed out for test: " << req_->ShortDebugString();
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_simulate_time_out_failures_msecs));
    return STATUS(TimedOut, "timed out for test");
  }

  if (server_.Clock()) {
    server::UpdateClock(*req_, server_.Clock());
  }

  // safe_ht_to_read is used only for read restart, so if read_time is valid, then we would respond
  // with "restart required".
  read_time_ = ReadHybridTime::FromReadTimePB(*req_);

  allow_retry_ = !read_time_;
  require_lease_ = tablet::RequireLease(req_->consistency_level() == YBConsistencyLevel::STRONG);
  // TODO: should check all the tables referenced by the requests to decide if it is transactional.
  const bool transactional = this->transactional();
  // Should not pick read time for serializable isolation, since it is picked after read intents
  // are added. Also conflict resolution for serializable isolation should be done without read time
  // specified. So we use max hybrid time for conflict resolution in such case.
  // It was implemented as part of #655.
  if (!serializable_isolation) {
    RETURN_NOT_OK(PickReadTime(server_.Clock()));
  }

  if (transactional) {
    // TODO(wait-queues) -- having this RequestScope live during conflict resolution may prevent
    // intent cleanup for any transactions resolved after this is created and before it's destroyed.
    // This may be especially problematic for operations which need to wait, as their waiting may
    // now cause intents_db scans to become less performant. Moving this initialization to only
    // cover cases where we avoid writes may cause inconsistency issues, as exposed by
    // PgOnConflictTest.OnConflict which fails if we move this code below.
    request_scope_ = VERIFY_RESULT(RequestScope::Create(tablet()->transaction_participant()));
    // Serial number is used to check whether this operation was initiated before
    // transaction status request. So we should initialize it as soon as possible.
    read_time_.serial_no = request_scope_.request_id();
  }

  const auto& remote_address = context_.remote_address();
  host_port_pb_.set_host(remote_address.address().to_string());
  host_port_pb_.set_port(remote_address.port());

  if (serializable_isolation || has_row_mark) {
    auto query = std::make_unique<tablet::WriteQuery>(
        leader_peer.leader_term,
        context_.GetClientDeadline(),
        leader_peer.peer.get(),
        leader_peer.tablet,
        nullptr /* rpc_context */);

    auto& write = *query->operation().AllocateRequest();
    auto& write_batch = *write.mutable_write_batch();
    *write_batch.mutable_transaction() = req_->transaction();
    if (has_row_mark) {
      write_batch.set_row_mark_type(batch_row_mark);
      query->set_read_time(read_time_);
    }
    write.ref_unused_tablet_id(""); // For backward compatibility.
    write_batch.set_deprecated_may_have_metadata(true);
    write.set_batch_idx(req_->batch_idx());
    if (req_->has_subtransaction() && req_->subtransaction().has_subtransaction_id()) {
      write_batch.mutable_subtransaction()->set_subtransaction_id(
          req_->subtransaction().subtransaction_id());
    }
    // TODO(dtxn) write request id

    RETURN_NOT_OK(leader_peer.tablet->CreateReadIntents(
        req_->transaction(), req_->subtransaction(), req_->ql_batch(), req_->pgsql_batch(),
        &write_batch));

    query->AdjustYsqlQueryTransactionality(req_->pgsql_batch_size());

    query->set_callback([peer = leader_peer.peer, self = shared_from_this()](const Status& status) {
      if (!status.ok()) {
        self->RespondFailure(status);
      } else {
        self->retained_self_ = self;
        peer->Enqueue(self.get());
      }
    });
    leader_peer.peer->WriteAsync(std::move(query));
    return Status::OK();
  }

  return Complete();
}

Status ReadQuery::DoPickReadTime(server::Clock* clock) {
  auto* metrics = abstract_tablet_->system() ? nullptr : tablet()->metrics();
  MonoTime start_time;
  if (metrics) {
    start_time = MonoTime::Now();
  }
  if (!read_time_) {
    safe_ht_to_read_ = VERIFY_RESULT(abstract_tablet_->SafeTime(require_lease_));
    // If the read time is not specified, then it is a single-shard read.
    // So we should restart it in server in case of failure.
    read_time_.read = safe_ht_to_read_;
    if (transactional()) {
      read_time_.global_limit = clock->MaxGlobalNow();
      read_time_.local_limit = std::min(safe_ht_to_read_, read_time_.global_limit);

      VLOG(1) << "Read time: " << read_time_.ToString();
    } else {
      read_time_.local_limit = read_time_.read;
      read_time_.global_limit = read_time_.read;
    }
  } else {
    HybridTime current_safe_time = HybridTime::kMin;
    if (IsPgsqlFollowerReadAtAFollower()) {
      current_safe_time = VERIFY_RESULT(abstract_tablet_->SafeTime(
          require_lease_, HybridTime::kMin, context_.GetClientDeadline()));
      if (GetAtomicFlag(&FLAGS_ysql_follower_reads_avoid_waiting_for_safe_time) &&
          current_safe_time < read_time_.read) {
        // We are given a read time. However, for Follower reads, it may be better
        // to redirect the query to the Leader instead of waiting on it.
        return STATUS(IllegalState, "Requested read time is not safe at this follower.");
      }
    }
    safe_ht_to_read_ =
        (current_safe_time > read_time_.read
             ? current_safe_time
             : VERIFY_RESULT(abstract_tablet_->SafeTime(
                   require_lease_, read_time_.read, context_.GetClientDeadline())));
  }
  if (metrics) {
    auto safe_time_wait = MonoTime::Now() - start_time;
    metrics->Increment(
         tablet::TabletHistograms::kReadTimeWait,
         make_unsigned(safe_time_wait.ToMicroseconds()));
  }
  return Status::OK();
}

bool ReadQuery::IsPgsqlFollowerReadAtAFollower() const {
  return reading_from_non_leader_ &&
         (!req_->pgsql_batch().empty() &&
          req_->consistency_level() == YBConsistencyLevel::CONSISTENT_PREFIX);
}

Status ReadQuery::Complete() {
  for (;;) {
    resp_->Clear();
    context_.sidecars().Reset();
    VLOG(1) << "Read time: " << read_time_ << ", safe: " << safe_ht_to_read_;
    const auto result = VERIFY_RESULT(DoRead());
    if (allow_retry_ && read_time_ && read_time_ == result) {
      YB_LOG_EVERY_N_SECS(DFATAL, 5)
          << __func__ << ", restarting read with the same read time: " << result << THROTTLE_MSG;
      allow_retry_ = false;
    }
    read_time_ = result;
    // If read was successful, then restart time is invalid. Finishing.
    // (If a read restart was requested, then read_time would be set to the time at which we have
    // to restart.)
    if (!read_time_) {
      // allow_retry means that the read time was not set in the request and therefore we can
      // retry read restarts on the tablet server.
      if (!allow_retry_) {
        auto local_limit = std::min(safe_ht_to_read_, used_read_time_.global_limit);
        resp_->set_local_limit_ht(local_limit.ToUint64());
      }
      break;
    }
    if (!allow_retry_) {
      // If the read time is specified, then we read as part of a transaction. So we should restart
      // whole transaction. In this case we report restart time and abort reading.
      resp_->Clear();
      auto restart_read_time = resp_->mutable_restart_read_time();
      restart_read_time->set_read_ht(read_time_.read.ToUint64());
      restart_read_time->set_deprecated_max_of_read_time_and_local_limit_ht(
          read_time_.local_limit.ToUint64());
      restart_read_time->set_local_limit_ht(read_time_.local_limit.ToUint64());
      // Global limit is ignored by caller, so we don't set it.
      tablet()->metrics()->Increment(tablet::TabletCounters::kRestartReadRequests);
      break;
    }

    if (CoarseMonoClock::now() > context_.GetClientDeadline()) {
      TRACE("Read timed out");
      return STATUS(TimedOut, "Read timed out");
    }
  }
  if (req_->include_trace() && Trace::CurrentTrace() != nullptr) {
    resp_->set_trace_buffer(Trace::CurrentTrace()->DumpToString(true));
  }

  // In case read time was not specified (i.e. allow_retry is true)
  // we just picked a read time and we should communicate it back to the caller.
  if (allow_retry_) {
    used_read_time_.ToPB(resp_->mutable_used_read_time());
  }

  // Useful when debugging transactions
#if defined(DUMP_READ)
  if (read_context->req->has_transaction() && read_context->req->pgsql_batch().size() == 1 &&
      read_context->req->pgsql_batch()[0].partition_column_values().size() == 1 &&
      read_context->resp->pgsql_batch().size() == 1 &&
      read_context->resp->pgsql_batch()[0].rows_data_sidecar() == 0) {
    auto txn_id = CHECK_RESULT(FullyDecodeTransactionId(
        read_context->req->transaction().transaction_id()));
    auto value_slice = read_context->context->RpcSidecar(0).as_slice();
    auto num = BigEndian::Load64(value_slice.data());
    std::string result;
    if (num == 0) {
      result = "<NONE>";
    } else if (num == 1) {
      auto len = BigEndian::Load64(value_slice.data() + 14) - 1;
      result = Slice(value_slice.data() + 22, len).ToBuffer();
    } else {
      result = value_slice.ToDebugHexString();
    }
    auto key = read_context->req->pgsql_batch(0).partition_column_values(0).value().int32_value();
    LOG(INFO) << txn_id << " READ DONE: " << key << " = " << result;
  }
#endif

  MakeRpcOperationCompletionCallback<ReadResponsePB>(
      std::move(context_), resp_, server_.Clock())(Status::OK());
  TRACE("Done Read");

  return Status::OK();
}

Result<ReadHybridTime> ReadQuery::DoRead() {
  Result<ReadHybridTime> result{ReadHybridTime()};
  {
    LongOperationTracker long_operation_tracker("Read", 1s);
    result = DoReadImpl();
  }
  // Check transaction is still alive in case read was successful
  // and data has been written earlier into current tablet in context of current transaction.
  const auto* transaction =
      req_->has_transaction() ? &req_->transaction() : nullptr;
  if (result.ok() && transaction && transaction->isolation() == NON_TRANSACTIONAL) {
    const auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(transaction->transaction_id()));
    auto& txn_participant = *tablet()->transaction_participant();
    RETURN_NOT_OK(txn_participant.CheckAborted(txn_id));
  }
  return result;
}

Result<ReadHybridTime> ReadQuery::DoReadImpl() {
  docdb::ReadOperationData read_operation_data = {
    .deadline = context_.GetClientDeadline(),
    .read_time = {},
  };

  tablet::ScopedReadOperation read_tx;
  if (IsForBackfill()) {
    read_operation_data.read_time = read_time_;
  } else {
    read_tx = VERIFY_RESULT(
        tablet::ScopedReadOperation::Create(abstract_tablet_.get(), require_lease_, read_time_));
    read_operation_data.read_time = read_tx.read_time();
  }
  used_read_time_ = read_operation_data.read_time;
  if (!req_->redis_batch().empty()) {
    // Assert the primary table is a redis table.
    DCHECK_EQ(abstract_tablet_->table_type(), TableType::REDIS_TABLE_TYPE);
    auto count = req_->redis_batch_size();
    std::vector<Status> rets(count);
    CountDownLatch latch(count);
    for (int idx = 0; idx < count; idx++) {
      const RedisReadRequestPB& redis_read_req = req_->redis_batch(idx);
      Status &failed_status_ = rets[idx];
      auto cb = [&latch, &failed_status_] (const Status &status) -> void {
                  if (!status.ok())
                    failed_status_ = status;
                  latch.CountDown(1);
                };
      auto func = Bind(
          &HandleRedisReadRequestAsync,
          Unretained(abstract_tablet_.get()),
          read_operation_data,
          redis_read_req,
          Unretained(resp_->add_redis_batch()),
          cb);

      Status s;
      bool run_async = FLAGS_parallelize_read_ops && (idx != count - 1);
      if (run_async) {
        s = server_.tablet_manager()->read_pool()->SubmitClosure(func);
      }

      if (!s.ok() || !run_async) {
        func.Run();
      }
    }
    latch.Wait();
    std::vector<Status> failed;
    for (auto& status : rets) {
      if (!status.ok()) {
        failed.push_back(status);
      }
    }
    if (failed.size() == 0) {
      // TODO(dtxn) implement read restart for Redis.
      return ReadHybridTime();
    } else if (failed.size() == 1) {
      return failed[0];
    } else {
      return STATUS(Combined, VectorToString(failed));
    }
  }

  if (!req_->ql_batch().empty()) {
    // Assert the primary table is a YQL table.
    DCHECK_EQ(abstract_tablet_->table_type(), TableType::YQL_TABLE_TYPE);
    ReadRequestPB* mutable_req = const_cast<ReadRequestPB*>(req_);
    for (QLReadRequestPB& ql_read_req : *mutable_req->mutable_ql_batch()) {
      // Update the remote endpoint.
      ql_read_req.set_allocated_remote_endpoint(&host_port_pb_);
      ql_read_req.set_allocated_proxy_uuid(mutable_req->mutable_proxy_uuid());
      auto se = ScopeExit([&ql_read_req] {
        ql_read_req.release_remote_endpoint();
        ql_read_req.release_proxy_uuid();
      });

      tablet::QLReadRequestResult result;
      TRACE("Start HandleQLReadRequest");
      RETURN_NOT_OK(abstract_tablet_->HandleQLReadRequest(
          read_operation_data, ql_read_req, req_->transaction(), &result,
          &context_.sidecars().Start()));
      TRACE("Done HandleQLReadRequest");
      if (result.restart_read_ht.is_valid()) {
        return FormRestartReadHybridTime(result.restart_read_ht);
      }
      result.response.set_rows_data_sidecar(
          narrow_cast<int32_t>(context_.sidecars().Complete()));
      resp_->add_ql_batch()->Swap(&result.response);
    }
    return ReadHybridTime();
  }

  if (!req_->pgsql_batch().empty()) {
    ReadRequestPB* mutable_req = const_cast<ReadRequestPB*>(req_);
    size_t total_num_rows_read = 0;
    for (PgsqlReadRequestPB& pgsql_read_req : *mutable_req->mutable_pgsql_batch()) {
      tablet::PgsqlReadRequestResult result(&context_.sidecars().Start());
      TRACE("Start HandlePgsqlReadRequest");
      RETURN_NOT_OK(abstract_tablet_->HandlePgsqlReadRequest(
          read_operation_data, !allow_retry_ /* is_explicit_request_read_time */, pgsql_read_req,
          req_->transaction(), req_->subtransaction(), &result));

      total_num_rows_read += result.num_rows_read;

      TRACE("Done HandlePgsqlReadRequest");
      if (result.restart_read_ht.is_valid()) {
        return FormRestartReadHybridTime(result.restart_read_ht);
      }
      result.response.set_rows_data_sidecar(
          narrow_cast<int32_t>(context_.sidecars().Complete()));
      resp_->add_pgsql_batch()->Swap(&result.response);
    }

    if (req_->consistency_level() == YBConsistencyLevel::CONSISTENT_PREFIX &&
        total_num_rows_read > 0) {
      tablet()->metrics()->IncrementBy(
          tablet::TabletCounters::kPgsqlConsistentPrefixReadRows, total_num_rows_read);
    }
    return ReadHybridTime();
  }

  if (abstract_tablet_->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    return STATUS(NotSupported, "Transaction status table does not support read");
  }

  return ReadHybridTime();
}

} // namespace

void PerformRead(
    TabletServerIf* server, ReadTabletProvider* read_tablet_provider,
    const ReadRequestPB* req, ReadResponsePB* resp, rpc::RpcContext context) {
  auto read_query = std::make_shared<ReadQuery>(
      server, read_tablet_provider, req, resp, std::move(context));
  read_query->Perform();
}

}  // namespace tserver
}  // namespace yb
