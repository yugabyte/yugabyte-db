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

#include "yb/tserver/tablet_service.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "yb/ash/wait_state.h"

#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_pool.h"

#include "yb/common/ql_value.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"
#include "yb/consensus/leader_lease.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/docdb/cql_operation.h"
#include "yb/docdb/pgsql_operation.h"

#include "yb/dockv/reader_projection.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/qlexpr/index.h"
#include "yb/qlexpr/ql_rowblock.h"

#include "yb/rpc/sidecars.h"
#include "yb/rpc/thread_pool.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tablet/abstract_tablet.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/operations/split_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/read_result.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/write_query.h"

#include "yb/tserver/read_query.h"
#include "yb/tserver/service_util.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_error.h"

#include "yb/tserver/xcluster_safe_time_map.h"
#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/crc.h"
#include "yb/util/debug-util.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/faststring.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/math_util.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/pg_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_format.h"
#include "yb/util/status_fwd.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"
#include "yb/util/write_buffer.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/ysql_upgrade.h"

using namespace std::literals;  // NOLINT

DEPRECATE_FLAG(int32, scanner_default_batch_size_bytes, "10_2022");

DEPRECATE_FLAG(int32, scanner_max_batch_size_bytes, "10_2022");

DEPRECATE_FLAG(int32, scanner_batch_size_rows, "10_2022");

DEFINE_UNKNOWN_int32(max_wait_for_safe_time_ms, 5000,
             "Maximum time in milliseconds to wait for the safe time to advance when trying to "
             "scan at the given hybrid_time.");

DEFINE_RUNTIME_int32(num_concurrent_backfills_allowed, -1,
             "Maximum number of concurrent backfill jobs that is allowed to run.");

DEFINE_test_flag(bool, tserver_noop_read_write, false, "Respond NOOP to read/write.");

DEFINE_RUNTIME_uint64(index_backfill_upperbound_for_user_enforced_txn_duration_ms, 65000,
    "For Non-Txn tables, it is impossible to know at the tservers "
    "whether or not an 'old transaction' is still active. To avoid "
    "having such old transactions, we assume a bound on the duration "
    "of such transactions (during the backfill process) and wait "
    "it out. This flag denotes a conservative upper bound on the "
    "duration of such user enforced transactions.");
TAG_FLAG(index_backfill_upperbound_for_user_enforced_txn_duration_ms, evolving);

DEFINE_RUNTIME_int32(index_backfill_additional_delay_before_backfilling_ms, 0,
    "Operations that are received by the tserver, and have decided how "
    "the indexes need to be updated (based on the IndexPermission), will "
    "not be added to the list of current transactions until they are "
    "replicated/applied. This delay allows for the GetSafeTime method "
    "to wait for such operations to be replicated/applied. Ideally, this "
    "value should be set to be something larger than the raft-heartbeat-interval "
    "but can be as high as the client_rpc_timeout if we want to be more conservative.");
TAG_FLAG(index_backfill_additional_delay_before_backfilling_ms, evolving);

DEFINE_RUNTIME_int32(index_backfill_wait_for_old_txns_ms, 0,
    "Index backfill needs to wait for transactions that started before the "
    "WRITE_AND_DELETE phase to commit or abort before choosing a time for "
    "backfilling the index. This is the max time that the GetSafeTime call will "
    "wait for, before it resorts to attempt aborting old transactions. This is "
    "necessary to guard against the pathological active transaction that never "
    "commits from blocking the index backfill forever.");
TAG_FLAG(index_backfill_wait_for_old_txns_ms, evolving);

DEFINE_test_flag(double, respond_write_failed_probability, 0.0,
                 "Probability to respond that write request is failed");

DEFINE_test_flag(bool, rpc_delete_tablet_fail, false, "Should delete tablet RPC fail.");

DECLARE_bool(disable_alter_vs_write_mutual_exclusion);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_uint64(transaction_min_running_check_interval_ms);
DECLARE_int64(transaction_rpc_timeout_ms);

DEFINE_test_flag(int32, txn_status_table_tablet_creation_delay_ms, 0,
                 "Extra delay to slowdown creation of transaction status table tablet.");

DEFINE_test_flag(int32, leader_stepdown_delay_ms, 0,
                 "Amount of time to delay before starting a leader stepdown change.");

DEFINE_test_flag(int32, alter_schema_delay_ms, 0, "Delay before processing AlterSchema.");

DEFINE_test_flag(bool, disable_post_split_tablet_rbs_check, false,
                 "If true, bypass any checks made to reject remote boostrap requests for post "
                 "split tablets whose parent tablets are still present.");

DEFINE_test_flag(double, fail_tablet_split_probability, 0.0,
                 "Probability of failing in TabletServiceAdminImpl::SplitTablet.");

DEFINE_test_flag(bool, pause_tserver_get_split_key, false,
                 "Pause before processing a GetSplitKey request.");

DEFINE_test_flag(bool, fail_wait_for_ysql_backends_catalog_version, false,
                 "Fail any WaitForYsqlBackendsCatalogVersion requests received by this tserver.");

DECLARE_int32(heartbeat_interval_ms);
DECLARE_uint64(rocksdb_max_file_size_for_compaction);

DEFINE_UNKNOWN_bool(enable_ysql, true,
            "Enable YSQL on the cluster. This will cause yb-master to initialize sys catalog "
            "tablet from an initial snapshot (in case --initial_sys_catalog_snapshot_path is "
            "specified or can be auto-detected). Also each tablet server will start a PostgreSQL "
            "server as a child process.");

DECLARE_int32(ysql_transaction_abort_timeout_ms);
DECLARE_bool(ysql_yb_disable_wait_for_backends_catalog_version);

DEFINE_test_flag(bool, fail_alter_schema_after_abort_transactions, false,
                 "If true, setup an error status in AlterSchema and respond success to rpc call. "
                 "This failure should not cause the TServer to crash but "
                 "instead return an error message on the YSQL connection.");

DEFINE_test_flag(bool, txn_status_moved_rpc_force_fail, false,
                 "Force updates in transaction status location to fail.");

DEFINE_test_flag(bool, txn_status_moved_rpc_force_fail_retryable, true,
                 "Forced updates in transaction status location failure should be retryable "
                 "error.");

DEFINE_test_flag(int32, txn_status_moved_rpc_handle_delay_ms, 0,
                 "Inject delay to slowdown handling of updates in transaction status location.");

METRIC_DEFINE_gauge_uint64(server, ts_split_op_added, "Split OPs Added to Leader",
                           yb::MetricUnit::kOperations,
                           "Number of split operations added to the leader's Raft log.");

DECLARE_bool(ysql_enable_db_catalog_version_mode);

DEFINE_test_flag(bool, skip_aborting_active_transactions_during_schema_change, false,
                 "Skip aborting active transactions during schema change");

DEFINE_test_flag(
    bool, skip_force_superblock_flush, false,
    "Used in tests to skip superblock flush on tablet flush.");

DEFINE_test_flag(
    uint64, wait_row_mark_exclusive_count, 0, "Number of row mark exclusive reads to wait for.");

DEFINE_test_flag(
    int32, set_tablet_follower_lag_ms, 0,
    "What to report for tablet follower lag on this tserver in CheckTserverTabletHealth.");

DEFINE_test_flag(
    bool, pause_before_tablet_health_response, false,
    "Whether to pause before responding to CheckTserverTabletHealth.");

double TEST_delay_create_transaction_probability = 0;

namespace yb {
namespace tserver {

using consensus::ChangeConfigRequestPB;
using consensus::ChangeConfigResponsePB;
using consensus::Consensus;
using consensus::CONSENSUS_CONFIG_ACTIVE;
using consensus::CONSENSUS_CONFIG_COMMITTED;
using consensus::ConsensusConfigType;
using consensus::ConsensusRequestPB;
using consensus::GetLastOpIdRequestPB;
using consensus::GetNodeInstanceRequestPB;
using consensus::GetNodeInstanceResponsePB;
using consensus::LeaderLeaseStatus;
using consensus::LeaderStepDownRequestPB;
using consensus::LeaderStepDownResponsePB;
using consensus::RunLeaderElectionRequestPB;
using consensus::RunLeaderElectionResponsePB;
using consensus::StartRemoteBootstrapRequestPB;
using consensus::StartRemoteBootstrapResponsePB;
using consensus::UnsafeChangeConfigRequestPB;
using consensus::UnsafeChangeConfigResponsePB;
using consensus::VoteRequestPB;
using consensus::VoteResponsePB;

using google::protobuf::RepeatedPtrField;
using rpc::RpcContext;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;
using tablet::ChangeMetadataOperation;
using tablet::Tablet;
using tablet::TabletPeer;
using tablet::TabletPeerPtr;
using tablet::TabletStatusPB;
using tablet::TruncateOperation;
using tablet::OperationCompletionCallback;

namespace {

Result<std::shared_ptr<consensus::RaftConsensus>> GetConsensus(const TabletPeerPtr& tablet_peer) {
  auto result = tablet_peer->GetRaftConsensus();
  if (!result) {
    return result.status().CloneAndAddErrorCode(
        TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
  }
  return *result;
}

template<class RespClass>
std::shared_ptr<consensus::RaftConsensus> GetConsensusOrRespond(const TabletPeerPtr& tablet_peer,
                                                                RespClass* resp,
                                                                rpc::RpcContext* context) {
  auto result = GetConsensus(tablet_peer);
  if (!result.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), result.status(), context);
  }
  return result.get();
}

template<class RespClass>
bool GetConsensusOrRespond(const TabletPeerPtr& tablet_peer,
                           RespClass* resp,
                           rpc::RpcContext* context,
                           shared_ptr<Consensus>* consensus) {
  auto result = GetConsensus(tablet_peer);
  if (!result.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), result.status(), context);
    return false;
  }
  return (*consensus = result.get()) != nullptr;
}

} // namespace

template<class Resp>
bool TabletServiceImpl::CheckWriteThrottlingOrRespond(
    double score, tablet::TabletPeer* tablet_peer, Resp* resp, rpc::RpcContext* context) {
  // Check for memory pressure; don't bother doing any additional work if we've
  // exceeded the limit.
  auto status = CheckWriteThrottling(score, tablet_peer);
  if (!status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), status, context);
    return false;
  }

  return true;
}

typedef ListTabletsResponsePB::StatusAndSchemaPB StatusAndSchemaPB;
typedef ListMasterServersResponsePB::MasterServerAndTypePB MasterServerAndTypePB;

class WriteQueryCompletionCallback {
 public:
  WriteQueryCompletionCallback(
      tablet::TabletPeerPtr tablet_peer,
      std::shared_ptr<rpc::RpcContext> context,
      WriteResponsePB* response,
      tablet::WriteQuery* query,
      const server::ClockPtr& clock,
      bool trace,
      bool leader_term_set_in_request)
      : tablet_peer_(std::move(tablet_peer)),
        context_(std::move(context)),
        response_(response),
        query_(query),
        clock_(clock),
        include_trace_(trace),
        leader_term_set_in_request_(leader_term_set_in_request),
        trace_(include_trace_ ? Trace::CurrentTrace() : nullptr) {}

  void operator()(Status status) const {
    SCOPED_WAIT_STATUS(OnCpu_Active);
    VLOG(1) << __PRETTY_FUNCTION__ << " completing with status " << status;
    // When we don't need to return any data, we could return success on duplicate request.
    if (status.IsAlreadyPresent() &&
        query_->ql_write_ops()->empty() &&
        query_->pgsql_write_ops()->empty() &&
        query_->client_request()->redis_write_batch().empty()) {
      status = Status::OK();
    }

    TRACE("Write completing with status $0", yb::ToString(status));

    if (!status.ok()) {
      if (leader_term_set_in_request_ && status.IsAborted() &&
          status.message().Contains("Operation submitted in term")) {
        // Return a permanent error since term is set the request.
        status = STATUS_FORMAT(InvalidArgument, "Leader term changed");
      }

      LOG(INFO) << tablet_peer_->LogPrefix() << "Write failed: " << status;
      if (include_trace_ && trace_) {
        response_->set_trace_buffer(trace_->DumpToString(true));
      }
      SetupErrorAndRespond(get_error(), status, context_.get());
      return;
    }

    // Retrieve the rowblocks returned from the QL write operations and return them as RPC
    // sidecars. Populate the row schema also.
    faststring rows_data;
    for (const auto& ql_write_op : *query_->ql_write_ops()) {
      const auto& ql_write_req = ql_write_op->request();
      auto* ql_write_resp = ql_write_op->response();
      const auto* rowblock = ql_write_op->rowblock();
      SchemaToColumnPBs(rowblock->schema(), ql_write_resp->mutable_column_schemas());
      rowblock->Serialize(ql_write_req.client(), &context_->sidecars().Start());
      ql_write_resp->set_rows_data_sidecar(
          narrow_cast<int32_t>(context_->sidecars().Complete()));
    }

    if (include_trace_ && trace_) {
      response_->set_trace_buffer(trace_->DumpToString(true));
    }
    response_->set_propagated_hybrid_time(clock_->Now().ToUint64());
    context_->RespondSuccess();
    VLOG(1) << __PRETTY_FUNCTION__ << " RespondedSuccess";
  }

 private:
  TabletServerErrorPB* get_error() const {
    return response_->mutable_error();
  }

  tablet::TabletPeerPtr tablet_peer_;
  const std::shared_ptr<rpc::RpcContext> context_;
  WriteResponsePB* const response_;
  tablet::WriteQuery* const query_;
  server::ClockPtr clock_;
  const bool include_trace_;
  const bool leader_term_set_in_request_;
  scoped_refptr<Trace> trace_;
};

// Checksums the scan result.
class ScanResultChecksummer {
 public:
  ScanResultChecksummer() {}

  void HandleRow(const Schema& schema, const qlexpr::QLTableRow& row) {
    QLValue value;
    buffer_.clear();
    for (uint32_t col_index = 0; col_index != schema.num_columns(); ++col_index) {
      auto status = row.GetValue(schema.column_id(col_index), &value);
      if (!status.ok()) {
        LOG(WARNING) << "Column " << schema.column_id(col_index)
                     << " not found in " << row.ToString();
        continue;
      }
      buffer_.append(pointer_cast<const char*>(&col_index), sizeof(col_index));
      if (schema.column(col_index).is_nullable()) {
        uint8_t defined = value.IsNull() ? 0 : 1;
        buffer_.append(pointer_cast<const char*>(&defined), sizeof(defined));
      }
      if (!value.IsNull()) {
        value.value().AppendToString(&buffer_);
      }
    }
    crc_->Compute(buffer_.c_str(), buffer_.size(), &agg_checksum_, nullptr);
  }

  // Accessors for initializing / setting the checksum.
  uint64_t agg_checksum() const { return agg_checksum_; }

 private:
  crc::Crc* const crc_ = crc::GetCrc32cInstance();
  uint64_t agg_checksum_ = 0;
  std::string buffer_;
};

Result<std::shared_ptr<tablet::AbstractTablet>> TabletServiceImpl::GetTabletForRead(
  const TabletId& tablet_id, tablet::TabletPeerPtr tablet_peer,
  YBConsistencyLevel consistency_level, tserver::AllowSplitTablet allow_split_tablet) {
  return GetTablet(server_->tablet_peer_lookup(), tablet_id, std::move(tablet_peer),
                   consistency_level, allow_split_tablet);
}

TabletServiceImpl::TabletServiceImpl(TabletServerIf* server)
    : TabletServerServiceIf(server->MetricEnt()),
      server_(server) {
}

TabletServiceAdminImpl::TabletServiceAdminImpl(TabletServer* server)
    : TabletServerAdminServiceIf(DCHECK_NOTNULL(server)->MetricEnt()), server_(server) {
  ts_split_op_added_ = METRIC_ts_split_op_added.Instantiate(server->MetricEnt(), 0);
}

std::string TabletServiceAdminImpl::LogPrefix() const {
  return Format("P $0: ", server_->permanent_uuid());
}

void TabletServiceAdminImpl::BackfillDone(
    const tablet::ChangeMetadataRequestPB* req, ChangeMetadataResponsePB* resp,
    rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "BackfillDone", req, resp, &context)) {
    return;
  }
  DVLOG(3) << "Received BackfillDone RPC: " << req->DebugString();

  server::UpdateClock(*req, server_->Clock());

  // For now, we shall only allow this RPC on the leader.
  auto tablet =
      LookupLeaderTabletOrRespond(server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  auto operation = std::make_unique<ChangeMetadataOperation>(
      tablet.tablet, tablet.peer->log());
  operation->AllocateRequest()->CopyFrom(*req);

  operation->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));

  // Submit the alter schema op. The RPC will be responded to asynchronously.
  tablet.peer->Submit(std::move(operation), tablet.leader_term);
}

void TabletServiceAdminImpl::GetSafeTime(
    const GetSafeTimeRequestPB* req, GetSafeTimeResponsePB* resp, rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "GetSafeTime", req, resp, &context)) {
    return;
  }
  DVLOG(3) << "Received GetSafeTime RPC: " << req->DebugString();

  server::UpdateClock(*req, server_->Clock());

  // For now, we shall only allow this RPC on the leader.
  auto tablet =
      LookupLeaderTabletOrRespond(server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }
  const CoarseTimePoint& deadline = context.GetClientDeadline();
  HybridTime min_hybrid_time(HybridTime::kMin);
  if (req->has_min_hybrid_time_for_backfill()) {
    min_hybrid_time = HybridTime(req->min_hybrid_time_for_backfill());

    // For YSQL, it is not possible for transactions to exist that use the old permissions.  This is
    // especially the case after commit a1729c352896e919f462c614770da443b9982c0a introduces
    // ysql_yb_disable_wait_for_backends_catalog_version=false, which explicitly waits on all
    // possible transactions.  Before that commit, the best-effort, unsafe
    // ysql_yb_index_state_flags_update_delay=1000 setting was used to guarantee that.  Even though
    // correctness guarantees are generally flawed when solely relying on
    // ysql_yb_index_state_flags_update_delay=1000, make a best effort to reduce the scope of that
    // flaw by aborting transactions in case ysql_yb_disable_wait_for_backends_catalog_version=true.
    //
    // For YCQL Transactional tables, wait until there are no pending transactions that started
    // prior to min_hybrid_time. These may not have updated the index correctly, if they
    // happen to commit after the backfill scan, it is possible that they may miss updating
    // the index because the some operations may have taken place prior to min_hybrid_time.
    //
    // For YCQL Non-Txn tables, it is impossible to know at the tservers whether or not an "old
    // transaction" is still active. To avoid having such old transactions, we assume a
    // bound on the length of such transactions (during the backfill process) and wait it
    // out.
    if (tablet.tablet->table_type() == TableType::PGSQL_TABLE_TYPE &&
        !FLAGS_ysql_yb_disable_wait_for_backends_catalog_version) {
      // No need to wait-for/abort transactions.
    } else if (!tablet.tablet->transaction_participant()) {
      min_hybrid_time = min_hybrid_time.AddMilliseconds(
          FLAGS_index_backfill_upperbound_for_user_enforced_txn_duration_ms);
      VLOG(2) << "GetSafeTime called on a user enforced transaction tablet "
              << tablet.peer->tablet_id() << " will wait until "
              << min_hybrid_time << " is safe.";
    } else {
      // Add some extra delay to wait for operations being replicated to be
      // applied.
      SleepFor(MonoDelta::FromMilliseconds(
          FLAGS_index_backfill_additional_delay_before_backfilling_ms));

      auto txn_particpant = tablet.tablet->transaction_participant();
      auto wait_until = CoarseMonoClock::Now() + FLAGS_index_backfill_wait_for_old_txns_ms * 1ms;
      HybridTime min_running_ht;
      for (;;) {
        min_running_ht = txn_particpant->MinRunningHybridTime();
        if ((min_running_ht && min_running_ht >= min_hybrid_time) ||
            CoarseMonoClock::Now() > wait_until) {
          break;
        }
        VLOG(2) << "MinRunningHybridTime is " << min_running_ht
                << " need to wait for " << min_hybrid_time;
        SleepFor(MonoDelta::FromMilliseconds(FLAGS_transaction_min_running_check_interval_ms));
      }

      VLOG(2) << "Finally MinRunningHybridTime is " << min_running_ht;
      if (min_running_ht < min_hybrid_time) {
        VLOG(2) << "Aborting Txns that started prior to " << min_hybrid_time;
        auto s = txn_particpant->StopActiveTxnsPriorTo(min_hybrid_time, deadline);
        if (!s.ok()) {
          SetupErrorAndRespond(resp->mutable_error(), s, &context);
          return;
        }
      }
    }
  }

  auto safe_time = tablet.tablet->SafeTime(
      tablet::RequireLease::kTrue, min_hybrid_time, deadline);
  if (!safe_time.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), safe_time.status(), &context);
    return;
  }

  resp->set_safe_time(safe_time->ToUint64());
  resp->set_propagated_hybrid_time(server_->Clock()->Now().ToUint64());
  VLOG(1) << "Tablet " << tablet.peer->tablet_id()
          << " returning safe time " << yb::ToString(safe_time);

  context.RespondSuccess();
}

void TabletServiceAdminImpl::BackfillIndex(
    const BackfillIndexRequestPB* req, BackfillIndexResponsePB* resp, rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "BackfillIndex", req, resp, &context)) {
    return;
  }
  DVLOG(3) << "Received BackfillIndex RPC: " << req->DebugString();

  server::UpdateClock(*req, server_->Clock());

  // For now, we shall only allow this RPC on the leader.
  auto tablet =
      LookupLeaderTabletOrRespond(server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  if (req->indexes().empty()) {
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS(InvalidArgument, "No indexes given in request"),
        TabletServerErrorPB::OPERATION_NOT_SUPPORTED,
        &context);
    return;
  }

  const CoarseTimePoint &deadline = context.GetClientDeadline();
  const auto coarse_start = CoarseMonoClock::Now();
  {
    SCOPED_WAIT_STATUS(BackfillIndex_WaitForAFreeSlot);
    std::unique_lock<std::mutex> l(backfill_lock_);
    while (num_tablets_backfilling_ >= FLAGS_num_concurrent_backfills_allowed) {
      if (backfill_cond_.wait_until(l, deadline) == std::cv_status::timeout) {
        SetupErrorAndRespond(
            resp->mutable_error(),
            STATUS_FORMAT(ServiceUnavailable,
                          "Already running $0 backfill requests",
                          num_tablets_backfilling_),
            &context);
        return;
      }
    }
    num_tablets_backfilling_++;
  }
  auto se = ScopeExit([this] {
    std::unique_lock<std::mutex> l(this->backfill_lock_);
    this->num_tablets_backfilling_--;
    this->backfill_cond_.notify_all();
  });

  // Wait for SafeTime to get past read_at;
  const HybridTime read_at(req->read_at_hybrid_time());
  DVLOG(1) << "Waiting for safe time to be past " << read_at;
  const auto safe_time =
      tablet.tablet->SafeTime(tablet::RequireLease::kFalse, read_at, deadline);
  DVLOG(1) << "Got safe time " << safe_time.ToString();
  if (!safe_time.ok()) {
    LOG(ERROR) << "Could not get a good enough safe time " << safe_time.ToString();
    SetupErrorAndRespond(resp->mutable_error(), safe_time.status(), &context);
    return;
  }

  // Don't work on the request if we have had to wait more than 50%
  // of the time allocated to us for the RPC.
  // Backfill is a costly operation, we do not want to start working
  // on it if we expect the client (master) to time out the RPC and
  // force us to redo the work.
  const auto coarse_now = CoarseMonoClock::Now();
  if (deadline - coarse_now < coarse_now - coarse_start) {
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS_FORMAT(
            ServiceUnavailable, "Not enough time left $0", deadline - coarse_now),
        &context);
    return;
  }

  bool all_at_backfill = true;
  bool all_past_backfill = true;
  bool is_pg_table = tablet.tablet->table_type() == TableType::PGSQL_TABLE_TYPE;
  const auto index_map = tablet.peer->tablet_metadata()->index_map(req->indexed_table_id());
  std::vector<qlexpr::IndexInfo> indexes_to_backfill;
  std::vector<TableId> index_ids;
  for (const auto& idx : req->indexes()) {
    auto result = index_map->FindIndex(idx.table_id());
    if (result) {
      const auto* index_info = *result;
      indexes_to_backfill.push_back(*index_info);
      index_ids.push_back(index_info->table_id());

      IndexInfoPB idx_info_pb;
      index_info->ToPB(&idx_info_pb);
      if (!is_pg_table) {
        all_at_backfill &=
            idx_info_pb.index_permissions() == IndexPermissions::INDEX_PERM_DO_BACKFILL;
      } else {
        // YSQL tables don't use all the docdb permissions, so use this approximation.
        // TODO(jason): change this back to being like YCQL once we bring the docdb permission
        // DO_BACKFILL back (issue #6218).
        all_at_backfill &=
            idx_info_pb.index_permissions() == IndexPermissions::INDEX_PERM_WRITE_AND_DELETE;
      }
      all_past_backfill &=
          idx_info_pb.index_permissions() > IndexPermissions::INDEX_PERM_DO_BACKFILL;
    } else {
      LOG(WARNING) << "index " << idx.table_id() << " not found in tablet metadata";
      all_at_backfill = false;
      all_past_backfill = false;
    }
  }

  if (!all_at_backfill) {
    if (all_past_backfill) {
      // Change this to see if for all indexes: IndexPermission > DO_BACKFILL.
      LOG(WARNING) << "Received BackfillIndex RPC: " << req->DebugString()
                   << " after all indexes have moved past DO_BACKFILL. IndexMap is "
                   << AsString(index_map);
      // This is possible if this tablet completed the backfill. But the master failed over before
      // other tablets could complete.
      // The new master is redoing the backfill. We are safe to ignore this request.
      context.RespondSuccess();
      return;
    }

    uint32_t our_schema_version = tablet.peer->tablet_metadata()->schema_version();
    uint32_t their_schema_version = req->schema_version();
    DCHECK_NE(our_schema_version, their_schema_version);
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS_SUBSTITUTE(
            InvalidArgument,
            "Tablet has a different schema $0 vs $1. "
            "Requested index is not ready to backfill. IndexMap: $2",
            our_schema_version, their_schema_version, AsString(index_map)),
        TabletServerErrorPB::MISMATCHED_SCHEMA, &context);
    return;
  }

  Status backfill_status;
  std::string backfilled_until;
  std::unordered_set<TableId> failed_indexes;
  size_t number_rows_processed = 0;
  if (is_pg_table) {
    if (!req->has_namespace_name()) {
      SetupErrorAndRespond(
          resp->mutable_error(),
          STATUS(
              InvalidArgument,
              "Attempted backfill on YSQL table without supplying database name"),
          TabletServerErrorPB::OPERATION_NOT_SUPPORTED,
          &context);
      return;
    }
    backfill_status = tablet.tablet->BackfillIndexesForYsql(
        indexes_to_backfill,
        req->start_key(),
        deadline,
        read_at,
        server_->pgsql_proxy_bind_address(),
        req->namespace_name(),
        server_->GetSharedMemoryPostgresAuthKey(),
        &number_rows_processed,
        &backfilled_until);
    if (backfill_status.IsIllegalState()) {
      DCHECK_EQ(failed_indexes.size(), 0) << "We don't support batching in YSQL yet";
      for (const auto& idx_info : indexes_to_backfill) {
        failed_indexes.insert(idx_info.table_id());
      }
      DCHECK_EQ(failed_indexes.size(), 1) << "We don't support batching in YSQL yet";
    }
  } else if (tablet.tablet->table_type() == TableType::YQL_TABLE_TYPE) {
    backfill_status = tablet.tablet->BackfillIndexes(
        indexes_to_backfill,
        req->start_key(),
        deadline,
        read_at,
        &number_rows_processed,
        &backfilled_until,
        &failed_indexes);
  } else {
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS(InvalidArgument, "Attempted backfill on tablet of invalid table type"),
        TabletServerErrorPB::OPERATION_NOT_SUPPORTED,
        &context);
    return;
  }
  DVLOG(1) << "Tablet " << tablet.peer->tablet_id() << " backfilled indexes "
           << yb::ToString(index_ids) << " and got " << backfill_status
           << " backfilled until : " << backfilled_until;

  resp->set_backfilled_until(backfilled_until);
  resp->set_propagated_hybrid_time(server_->Clock()->Now().ToUint64());
  resp->set_number_rows_processed(number_rows_processed);

  if (!backfill_status.ok()) {
    VLOG(2) << " Failed indexes are " << yb::ToString(failed_indexes);
    for (const auto& idx : failed_indexes) {
      *resp->add_failed_index_ids() = idx;
    }
    SetupErrorAndRespond(
        resp->mutable_error(),
        backfill_status,
        (backfill_status.IsIllegalState()
            ? TabletServerErrorPB::OPERATION_NOT_SUPPORTED
            : TabletServerErrorPB::UNKNOWN_ERROR),
        &context);
    return;
  }

  context.RespondSuccess();
}

void TabletServiceAdminImpl::AlterSchema(const tablet::ChangeMetadataRequestPB* req,
                                         ChangeMetadataResponsePB* resp,
                                         rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "ChangeMetadata", req, resp, &context)) {
    return;
  }
  VLOG(1) << "Received Change Metadata RPC: " << req->DebugString();
  if (FLAGS_TEST_alter_schema_delay_ms) {
    LOG(INFO) << __func__ << ": sleeping for " << FLAGS_TEST_alter_schema_delay_ms << "ms";
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_alter_schema_delay_ms));
    LOG(INFO) << __func__ << ": done sleeping for " << FLAGS_TEST_alter_schema_delay_ms << "ms";
  }

  server::UpdateClock(*req, server_->Clock());

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  tablet::TableInfoPtr table_info;
  if (req->has_alter_table_id()) {
    auto result = tablet.peer->tablet_metadata()->GetTableInfo(req->alter_table_id());
    if (!result.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), result.status(),
                           TabletServerErrorPB::INVALID_SCHEMA, &context);
      return;
    }
    table_info = *result;
  } else {
    table_info = tablet.peer->tablet_metadata()->primary_table_info();
  }
  const Schema& tablet_schema = table_info->schema();
  uint32_t schema_version = table_info->schema_version;
  // Sanity check, to verify that the tablet should have the same schema
  // specified in the request.
  Schema req_schema;
  Status s = SchemaFromPB(req->schema(), &req_schema);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), s, TabletServerErrorPB::INVALID_SCHEMA, &context);
    return;
  }

  // If the schema was already applied, respond as succeeded.
  if (!req->has_wal_retention_secs() && schema_version == req->schema_version()) {

    if (req_schema.Equals(tablet_schema)) {
      context.RespondSuccess();
      return;
    }

    schema_version = tablet.peer->tablet_metadata()->schema_version(
        req->has_alter_table_id() ? req->alter_table_id() : "");
    if (schema_version == req->schema_version()) {
      LOG(ERROR) << "The current schema does not match the request schema."
                 << " version=" << schema_version
                 << " current-schema=" << tablet_schema.ToString()
                 << " request-schema=" << req_schema.ToString()
                 << " (corruption)";
      SetupErrorAndRespond(resp->mutable_error(),
                           STATUS(Corruption, "got a different schema for the same version number"),
                           TabletServerErrorPB::MISMATCHED_SCHEMA, &context);
      return;
    }
  }

  // If the current schema is newer than the one in the request reject the request.
  if (schema_version > req->schema_version()) {
    LOG(ERROR) << "Tablet " << req->tablet_id() << " has a newer schema"
               << " version=" << schema_version
               << " req->schema_version()=" << req->schema_version()
               << "\n current-schema=" << tablet_schema.ToString()
               << "\n request-schema=" << req_schema.ToString();
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS_SUBSTITUTE(
            InvalidArgument, "Tablet has a newer schema Tab $0. Req $1 vs Existing version : $2",
            req->tablet_id(), req->schema_version(), schema_version),
        TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA, &context);
    return;
  }

  VLOG(1) << "Tablet updating schema from "
          << " version=" << schema_version << " current-schema=" << tablet_schema.ToString()
          << " to request-schema=" << req_schema.ToString()
          << " for table ID=" << table_info->table_id;
  ScopedRWOperationPause pause_writes;
  if ((tablet.tablet->table_type() == TableType::YQL_TABLE_TYPE &&
       !GetAtomicFlag(&FLAGS_disable_alter_vs_write_mutual_exclusion)) ||
      tablet.tablet->table_type() == TableType::PGSQL_TABLE_TYPE) {
    // For schema change operations we will have to pause the write operations
    // until the schema change is done. This will be done synchronously.
    pause_writes = tablet.tablet->PauseWritePermits(context.GetClientDeadline());
    if (!pause_writes.ok()) {
      SetupErrorAndRespond(
          resp->mutable_error(),
          STATUS(
              TryAgain, "Could not lock the tablet against write operations for schema change"),
          &context);
      return;
    }

    // After write operation is paused, active transactions will be aborted for YSQL transactions.
    if (tablet.tablet->table_type() == TableType::PGSQL_TABLE_TYPE &&
        req->should_abort_active_txns() &&
        !FLAGS_TEST_skip_aborting_active_transactions_during_schema_change) {
      DCHECK(req->has_transaction_id());
      if (tablet.tablet->transaction_participant() == nullptr) {
        auto status = STATUS(
            IllegalState, "Transaction participant is null for tablet " + req->tablet_id());
        LOG(ERROR) << status;
        SetupErrorAndRespond(
            resp->mutable_error(),
            status,
            &context);
        return;
      }
      HybridTime max_cutoff = HybridTime::kMax;
      CoarseTimePoint deadline =
          CoarseMonoClock::Now() +
          MonoDelta::FromMilliseconds(FLAGS_ysql_transaction_abort_timeout_ms);
      TransactionId txn_id = CHECK_RESULT(TransactionId::FromString(req->transaction_id()));
      LOG(INFO) << "Aborting transactions that started prior to " << max_cutoff
                << " for tablet id " << req->tablet_id()
                << " excluding transaction with id " << txn_id;
      // There could be a chance where a transaction does not appear by transaction_participant
      // but has already begun replicating through Raft. Such transactions might succeed rather
      // than get aborted. This race codnition is dismissable for this intermediate solution.
      Status status = tablet.tablet->transaction_participant()->StopActiveTxnsPriorTo(
            max_cutoff, deadline, &txn_id);
      if (!status.ok() || PREDICT_FALSE(FLAGS_TEST_fail_alter_schema_after_abort_transactions)) {
        auto status = STATUS(TryAgain, "Transaction abort failed for tablet " + req->tablet_id());
        LOG(WARNING) << status;
        SetupErrorAndRespond(
            resp->mutable_error(),
            status,
            &context);
        return;
      }
    }
  }
  auto operation = std::make_unique<ChangeMetadataOperation>(
      tablet.tablet, tablet.peer->log());
  auto request = operation->AllocateRequest();
  request->CopyFrom(*req);

  // CDC SDK Create Stream Context
  if (req->has_retention_requester_id()) {
    auto status = SetupCDCSDKRetention(req, resp, tablet.peer);
    if (!status.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), status, &context);
      return;
    }
  }

  operation->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));
  operation->UsePermitToken(std::move(pause_writes));

  // Submit the alter schema op. The RPC will be responded to asynchronously.
  tablet.peer->Submit(std::move(operation), tablet.leader_term);
}

Status TabletServiceAdminImpl::SetupCDCSDKRetention(const tablet::ChangeMetadataRequestPB* req,
                                                    ChangeMetadataResponsePB* resp,
                                                    const TabletPeerPtr& tablet_peer) {

  tablet::RemoveIntentsData data;
  auto s = tablet_peer->GetLastReplicatedData(&data);
  if (s.ok()) {
    LOG_WITH_PREFIX(INFO) << "Proposed cdc_sdk_snapshot_safe_op_id: " << data.op_id
                          << ", time: " << data.log_ht;
    resp->mutable_cdc_sdk_snapshot_safe_op_id()->set_term(data.op_id.term);
    resp->mutable_cdc_sdk_snapshot_safe_op_id()->set_index(data.op_id.index);
  } else {
    LOG_WITH_PREFIX(WARNING) << "CDCSDK Create Stream context: "
                             << "Could not get snapshot_safe_opid: "
                             << s;
    return s;
  }

  // Now, from this point on, till the Followers Apply the ChangeMetadataOperation,
  // any proposed history cutoff they process can only be less than Now()
  auto require_history_cutoff =
      req->has_cdc_sdk_require_history_cutoff() && req->cdc_sdk_require_history_cutoff();
  auto res = tablet_peer->SetAllInitialCDCSDKRetentionBarriers(
      data.op_id, server_->Clock()->Now(), require_history_cutoff);
  // If there was an error while setting the retention barrier cutoff, respond with an error
  if (!res.ok()) {
    WARN_NOT_OK(res, "CDCSDK Create Stream context: Unable to set history cutoff");
    RETURN_NOT_OK(res);
  }

  return Status::OK();
}


#define VERIFY_RESULT_OR_RETURN(expr) RESULT_CHECKER_HELPER( \
    expr, \
    if (!__result.ok()) { return; });

void TabletServiceImpl::VerifyTableRowRange(
    const VerifyTableRowRangeRequestPB* req,
    VerifyTableRowRangeResponsePB* resp,
    rpc::RpcContext context) {
  DVLOG(3) << "Received VerifyTableRowRange RPC: " << req->DebugString();

  server::UpdateClock(*req, server_->Clock());

  auto peer_tablet =
      LookupTabletPeerOrRespond(server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!peer_tablet) {
    return;
  }

  auto tablet = peer_tablet->tablet;
  bool is_pg_table = tablet->table_type() == TableType::PGSQL_TABLE_TYPE;
  if (is_pg_table) {
    SetupErrorAndRespond(
        resp->mutable_error(), STATUS(NotFound, "Verify operation not supported for PGSQL tables."),
        &context);
    return;
  }

  const CoarseTimePoint& deadline = context.GetClientDeadline();

  // Wait for SafeTime to get past read_at;
  const HybridTime read_at(req->read_time());
  DVLOG(1) << "Waiting for safe time to be past " << read_at;
  const auto safe_time = tablet->SafeTime(tablet::RequireLease::kFalse, read_at, deadline);
  DVLOG(1) << "Got safe time " << safe_time.ToString();
  if (!safe_time.ok()) {
    LOG(ERROR) << "Could not get a good enough safe time " << safe_time.ToString();
    SetupErrorAndRespond(resp->mutable_error(), safe_time.status(), &context);
    return;
  }

  auto valid_read_at = req->has_read_time() ? read_at : *safe_time;
  std::string verified_until = "";
  std::unordered_map<TableId, uint64> consistency_stats;

  if (peer_tablet->tablet_peer->tablet_metadata()->primary_table_info()->index_info) {
    auto index_info =
        *peer_tablet->tablet_peer->tablet_metadata()->primary_table_info()->index_info;
    const auto& table_id = index_info.indexed_table_id();
    Status verify_status = tablet->VerifyMainTableConsistencyForCQL(
        table_id, req->start_key(), req->num_rows(), deadline, valid_read_at, &consistency_stats,
        &verified_until);
    if (!verify_status.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), verify_status, &context);
      return;
    }

    (*resp->mutable_consistency_stats())[table_id] = consistency_stats[table_id];
  } else {
    const auto& index_map =
        *peer_tablet->tablet_peer->tablet_metadata()->primary_table_info()->index_map;
    vector<qlexpr::IndexInfo> indexes;
    vector<TableId> index_ids;
    if (req->index_ids().empty()) {
      for (auto it = index_map.begin(); it != index_map.end(); it++) {
        indexes.push_back(it->second);
      }
    } else {
      for (const auto& idx : req->index_ids()) {
        auto result = index_map.FindIndex(idx);
        if (result) {
          const auto* index_info = *result;
          indexes.push_back(*index_info);
          index_ids.push_back(index_info->table_id());
        } else {
          LOG(WARNING) << "Index " << idx << " not found in tablet metadata";
        }
      }
    }

    Status verify_status = tablet->VerifyIndexTableConsistencyForCQL(
        indexes, req->start_key(), req->num_rows(), deadline, valid_read_at, &consistency_stats,
        &verified_until);
    if (!verify_status.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), verify_status, &context);
      return;
    }

    for (const auto& index : indexes) {
      const auto& table_id = index.table_id();
      (*resp->mutable_consistency_stats())[table_id] = consistency_stats[table_id];
    }
  }
  resp->set_verified_until(verified_until);
  context.RespondSuccess();
}

void TabletServiceImpl::UpdateTransaction(const UpdateTransactionRequestPB* req,
                                          UpdateTransactionResponsePB* resp,
                                          rpc::RpcContext context) {
  TRACE("UpdateTransaction");

  if (req->state().status() == TransactionStatus::CREATED &&
      RandomActWithProbability(TEST_delay_create_transaction_probability)) {
    std::this_thread::sleep_for(
        (FLAGS_transaction_rpc_timeout_ms + RandomUniformInt(-200, 200)) * 1ms);
  }

  VLOG(1) << "UpdateTransaction: " << req->ShortDebugString()
          << ", context: " << context.ToString();
  LOG_IF(DFATAL, !req->has_propagated_hybrid_time())
      << __func__ << " missing propagated hybrid time for "
      << TransactionStatus_Name(req->state().status());
  UpdateClock(*req, server_->Clock());

  LeaderTabletPeer tablet;
  auto txn_status = req->state().status();
  auto cleanup = txn_status == TransactionStatus::IMMEDIATE_CLEANUP ||
                 txn_status == TransactionStatus::GRACEFUL_CLEANUP;
  if (cleanup) {
    auto peer_tablet = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
        server_->tablet_peer_lookup(), req->tablet_id(), resp, &context));
    tablet.FillTabletPeer(std::move(peer_tablet));
    tablet.leader_term = OpId::kUnknownTerm;
  } else {
    tablet = LookupLeaderTabletOrRespond(
        server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  }
  if (!tablet) {
    return;
  }

  auto state = std::make_unique<tablet::UpdateTxnOperation>(tablet.tablet);
  state->AllocateRequest()->CopyFrom(req->state());
  state->set_completion_callback(MakeRpcOperationCompletionCallback(
      std::move(context), resp, server_->Clock()));

  if (req->state().status() == TransactionStatus::APPLYING || cleanup) {
    auto* participant = tablet.tablet->transaction_participant();
    if (participant) {
      participant->Handle(std::move(state), tablet.leader_term);
    } else {
      state->CompleteWithStatus(STATUS_FORMAT(
          InvalidArgument, "Does not have transaction participant to process $0",
          req->state().status()));
    }
  } else {
    auto* coordinator = tablet.tablet->transaction_coordinator();
    if (coordinator) {
      coordinator->Handle(std::move(state), tablet.leader_term);
    } else {
      state->CompleteWithStatus(STATUS_FORMAT(
          InvalidArgument, "Does not have transaction coordinator to process $0",
          req->state().status()));
    }
  }
}

template <class Req, class Resp, class Action>
void TabletServiceImpl::PerformAtLeader(
    const Req& req, Resp* resp, rpc::RpcContext* context, const Action& action) {
  UpdateClock(*req, server_->Clock());

  auto tablet_peer = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, context);

  if (!tablet_peer) {
    return;
  }

  auto status = action(tablet_peer);

  if (*context) {
    resp->set_propagated_hybrid_time(server_->Clock()->Now().ToUint64());
    if (status.ok()) {
      context->RespondSuccess();
    } else {
      SetupErrorAndRespond(resp->mutable_error(), status, context);
    }
  }
}

void TabletServiceImpl::GetTransactionStatus(const GetTransactionStatusRequestPB* req,
                                             GetTransactionStatusResponsePB* resp,
                                             rpc::RpcContext context) {
  TRACE("GetTransactionStatus");

  PerformAtLeader(req, resp, &context,
      [req, resp, &context](const LeaderTabletPeer& tablet_peer) {
    auto* transaction_coordinator = tablet_peer.tablet->transaction_coordinator();
    if (!transaction_coordinator) {
      return STATUS_FORMAT(
          InvalidArgument, "No transaction coordinator at tablet $0",
          tablet_peer.peer->tablet_id());
    }
    return transaction_coordinator->GetStatus(
        req->transaction_id(), context.GetClientDeadline(), resp);
  });
}

void TabletServiceImpl::GetOldTransactions(const GetOldTransactionsRequestPB* req,
                                           GetOldTransactionsResponsePB* resp,
                                           rpc::RpcContext context) {
  TRACE("GetOldTransactions");

  PerformAtLeader(req, resp, &context,
      [req, resp, &context](const LeaderTabletPeer& tablet_peer) {
    auto* transaction_coordinator = tablet_peer.tablet->transaction_coordinator();
    if (!transaction_coordinator) {
      return STATUS_FORMAT(
          InvalidArgument, "No transaction coordinator at tablet $0",
          tablet_peer.peer->tablet_id());
    }
    return transaction_coordinator->GetOldTransactions(req, resp, context.GetClientDeadline());
  });
}

void TabletServiceImpl::GetTransactionStatusAtParticipant(
    const GetTransactionStatusAtParticipantRequestPB* req,
    GetTransactionStatusAtParticipantResponsePB* resp,
    rpc::RpcContext context) {
  TRACE("GetTransactionStatusAtParticipant");

  PerformAtLeader(req, resp, &context,
      [req, resp, &context](const LeaderTabletPeer& tablet_peer) -> Status {
    auto* transaction_participant = tablet_peer.tablet->transaction_participant();
    if (!transaction_participant) {
      return STATUS_FORMAT(
          InvalidArgument, "No transaction participant at tablet $0",
          tablet_peer.peer->tablet_id());
    }

    transaction_participant->GetStatus(
        VERIFY_RESULT(FullyDecodeTransactionId(req->transaction_id())),
        req->required_num_replicated_batches(), tablet_peer.leader_term, resp, &context);
    return Status::OK();
  });
}

void TabletServiceImpl::AbortTransaction(const AbortTransactionRequestPB* req,
                                         AbortTransactionResponsePB* resp,
                                         rpc::RpcContext context) {
  TRACE("AbortTransaction");

  UpdateClock(*req, server_->Clock());

  auto id_or_status = FullyDecodeTransactionId(req->transaction_id());
  if (!id_or_status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), id_or_status.status(), &context);
    return;
  }
  const auto& txn_id = *id_or_status;

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  server::ClockPtr clock(server_->Clock());
  auto context_ptr = std::make_shared<rpc::RpcContext>(std::move(context));
  tablet.tablet->transaction_coordinator()->Abort(
      txn_id,
      tablet.leader_term,
      [resp, context_ptr, clock, peer = tablet.peer](Result<TransactionStatusResult> result) {
        resp->set_propagated_hybrid_time(clock->Now().ToUint64());
        Status status;
        if (result.ok()) {
          auto leader_safe_time = peer->LeaderSafeTime();
          if (leader_safe_time.ok()) {
            resp->set_status(result->status);
            if (result->status_time.is_valid()) {
              resp->set_status_hybrid_time(result->status_time.ToUint64());
            }
            // See comment above WaitForSafeTime in TransactionStatusCache::DoGetCommitData
            // for details.
            resp->set_coordinator_safe_time(leader_safe_time->ToUint64());
            context_ptr->RespondSuccess();
            return;
          }

          status = leader_safe_time.status();
        } else {
          status = result.status();
        }
        SetupErrorAndRespond(resp->mutable_error(), status, context_ptr.get());
      });
}

void TabletServiceImpl::UpdateTransactionStatusLocation(
    const UpdateTransactionStatusLocationRequestPB* req,
    UpdateTransactionStatusLocationResponsePB* resp,
    rpc::RpcContext context) {
  TRACE("UpdateTransactionStatusLocation");

  VLOG(1) << "UpdateTransactionStatusLocation: " << req->ShortDebugString()
          << ", context: " << context.ToString();

  auto context_ptr = std::make_shared<rpc::RpcContext>(std::move(context));
  auto status = HandleUpdateTransactionStatusLocation(req, resp, context_ptr);
  if (!status.ok()) {
    LOG(WARNING) << status;
    SetupErrorAndRespond(resp->mutable_error(), status, context_ptr.get());
  }
}

Status TabletServiceImpl::HandleUpdateTransactionStatusLocation(
    const UpdateTransactionStatusLocationRequestPB* req,
    UpdateTransactionStatusLocationResponsePB* resp,
    std::shared_ptr<rpc::RpcContext> context) {
  LOG_IF(DFATAL, !req->has_propagated_hybrid_time())
      << __func__ << " missing propagated hybrid time for transaction status location update";
  UpdateClock(*req, server_->Clock());

  if (PREDICT_FALSE(FLAGS_TEST_txn_status_moved_rpc_handle_delay_ms > 0)) {
    std::this_thread::sleep_for(FLAGS_TEST_txn_status_moved_rpc_handle_delay_ms * 1ms);
  }

  if (PREDICT_FALSE(FLAGS_TEST_txn_status_moved_rpc_force_fail)) {
    if (FLAGS_TEST_txn_status_moved_rpc_force_fail_retryable) {
      return STATUS(IllegalState, "UpdateTransactionStatusLocation forced to fail");
    } else {
      return STATUS(Expired, "UpdateTransactionStatusLocation forced to fail");
    }
  }

  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req->transaction_id()));

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, context.get());
  if (!tablet) {
    return Status::OK();
  }

  auto* participant = tablet.tablet->transaction_participant();
  if (!participant) {
    return STATUS(InvalidArgument, "No transaction participant to process transaction status move");
  }

  auto metadata = participant->UpdateTransactionStatusLocation(txn_id, req->new_status_tablet_id());
  if (!metadata.ok()) {
    return metadata.status();
  }

  auto query = std::make_unique<tablet::WriteQuery>(
      tablet.leader_term, context->GetClientDeadline(), tablet.peer.get(),
      tablet.tablet, nullptr);
  auto* request = query->operation().AllocateRequest();
  metadata->ToPB(request->mutable_write_batch()->mutable_transaction());

  query->set_callback([resp, context](const Status& status) {
    if (!status.ok()) {
      LOG(WARNING) << status;
      SetupErrorAndRespond(resp->mutable_error(), status, context.get());
    } else {
      context->RespondSuccess();
    }
  });
  tablet.peer->WriteAsync(std::move(query));

  return Status::OK();
}

void TabletServiceImpl::UpdateTransactionWaitingForStatus(
    const UpdateTransactionWaitingForStatusRequestPB* req,
    UpdateTransactionWaitingForStatusResponsePB* resp,
    rpc::RpcContext context) {
  UpdateClock(*req, server_->Clock());

  auto tablet = LookupLeaderTabletOrRespond(
    server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  tablet.tablet->transaction_coordinator()->ProcessWaitForReport(
      *req, resp, MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));
}

void TabletServiceImpl::ProbeTransactionDeadlock(
    const ProbeTransactionDeadlockRequestPB* req,
    ProbeTransactionDeadlockResponsePB* resp,
    rpc::RpcContext context) {
  UpdateClock(*req, server_->Clock());

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  tablet.tablet->transaction_coordinator()->ProcessProbe(
      *req, resp, MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));
}

void TabletServiceImpl::Truncate(const TruncateRequestPB* req,
                                 TruncateResponsePB* resp,
                                 rpc::RpcContext context) {
  TRACE("Truncate");

  UpdateClock(*req, server_->Clock());

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  auto operation = std::make_unique<TruncateOperation>(tablet.tablet);
  operation->AllocateRequest()->CopyFrom(req->truncate());

  operation->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));

  // Submit the truncate tablet op. The RPC will be responded to asynchronously.
  tablet.peer->Submit(std::move(operation), tablet.leader_term);
}

void TabletServiceImpl::GetCompatibleSchemaVersion(
    const GetCompatibleSchemaVersionRequestPB* req,
    GetCompatibleSchemaVersionResponsePB* resp,
    rpc::RpcContext context) {
  VLOG(1) << "Full request: " << req->DebugString();
  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  Schema req_schema;
  Status s = SchemaFromPB(req->schema(), &req_schema);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), s, TabletServerErrorPB::INVALID_SCHEMA, &context);
    return;
  }

  tablet::TableInfoPtr table_info = nullptr;
  if (req->schema().has_colocated_table_id()) {
    auto result = tablet.peer->tablet_metadata()->GetTableInfo(
        req->schema().colocated_table_id().colocation_id());
    if (!result.ok()) {
      SetupErrorAndRespond(
          resp->mutable_error(), result.status(), TabletServerErrorPB::TABLET_NOT_FOUND, &context);
      return;
    }

    table_info = *result;
  } else {
    table_info = tablet.peer->tablet_metadata()->primary_table_info();
  }

  const Schema& consumer_schema = table_info->schema();
  SchemaVersion schema_version = table_info->schema_version;

  // Check is the current schema already matches. If not,
  // check if there is a compatible schema packing and return
  // the associated schema version.
  if (!req_schema.EquivalentForDataCopy(consumer_schema)) {
    auto result = table_info->GetSchemaPackingVersion(req_schema);
    if (result.ok()) {
      schema_version = *result;
    } else {
      SetupErrorAndRespond(
          resp->mutable_error(), result.status(), TabletServerErrorPB::MISMATCHED_SCHEMA, &context);
      return;
    }
  }

  // Set the compatible consumer schema version and respond.
  VLOG_WITH_FUNC(2) << Format("Compatible schema version $0 for schema $1 found on tablet $2.",
                              schema_version, req_schema.ToString(), req->tablet_id());
  resp->set_compatible_schema_version(schema_version);
  context.RespondSuccess();
}

void TabletServiceAdminImpl::CreateTablet(const CreateTabletRequestPB* req,
                                          CreateTabletResponsePB* resp,
                                          rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "CreateTablet", req, resp, &context)) {
    return;
  }
  auto status = DoCreateTablet(req, resp);
  if (!status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), status, &context);
  } else {
    context.RespondSuccess();
  }
}

Status TabletServiceAdminImpl::DoCreateTablet(const CreateTabletRequestPB* req,
                                              CreateTabletResponsePB* resp) {
  if (PREDICT_FALSE(FLAGS_TEST_txn_status_table_tablet_creation_delay_ms > 0 &&
                    req->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE)) {
    std::this_thread::sleep_for(FLAGS_TEST_txn_status_table_tablet_creation_delay_ms * 1ms);
  }

  DVLOG(3) << "Received CreateTablet RPC: " << yb::ToString(*req);
  TRACE_EVENT1("tserver", "CreateTablet",
               "tablet_id", req->tablet_id());

  Schema schema;
  dockv::PartitionSchema partition_schema;
  auto status = SchemaFromPB(req->schema(), &schema);
  if (status.ok()) {
    DCHECK(schema.has_column_ids());
    status = dockv::PartitionSchema::FromPB(req->partition_schema(), schema, &partition_schema);
  }
  if (!status.ok()) {
    return status.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::INVALID_SCHEMA));
  }

  dockv::Partition partition;
  dockv::Partition::FromPB(req->partition(), &partition);

  LOG(INFO) << "Processing CreateTablet for T " << req->tablet_id() << " P " << req->dest_uuid()
            << " (table=" << req->table_name()
            << " [id=" << req->table_id() << "]), partition="
            << partition_schema.PartitionDebugString(partition, schema);
  VLOG(1) << "Full request: " << req->DebugString();

  auto table_info = std::make_shared<tablet::TableInfo>(
      consensus::MakeTabletLogPrefix(req->tablet_id(), server_->permanent_uuid()),
      tablet::Primary::kTrue, req->table_id(), req->namespace_name(), req->table_name(),
      req->table_type(), schema, qlexpr::IndexMap(),
      req->has_index_info() ? boost::optional<qlexpr::IndexInfo>(req->index_info()) : boost::none,
      0 /* schema_version */, partition_schema, req->pg_table_id());
  std::vector<SnapshotScheduleId> snapshot_schedules;
  snapshot_schedules.reserve(req->snapshot_schedules().size());
  for (const auto& id : req->snapshot_schedules()) {
    snapshot_schedules.push_back(VERIFY_RESULT(FullyDecodeSnapshotScheduleId(id)));
  }

  std::unordered_set<StatefulServiceKind> hosted_services;
  for (auto& service_kind : req->hosted_stateful_services()) {
    SCHECK(
        StatefulServiceKind_IsValid(service_kind), InvalidArgument,
        Format("Invalid stateful service kind: $0", service_kind));
    hosted_services.insert((StatefulServiceKind)service_kind);
  }

  status = ResultToStatus(server_->tablet_manager()->CreateNewTablet(
      table_info, req->tablet_id(), partition, req->config(), req->colocated(), snapshot_schedules,
      hosted_services));
  if (PREDICT_FALSE(!status.ok())) {
    return status.IsAlreadyPresent()
        ? status.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::TABLET_ALREADY_EXISTS))
        : status;
  }
  return Status::OK();
}

void TabletServiceAdminImpl::PrepareDeleteTransactionTablet(
    const PrepareDeleteTransactionTabletRequestPB* req,
    PrepareDeleteTransactionTabletResponsePB* resp,
    rpc::RpcContext context) {
  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }
  auto coordinator = tablet.tablet->transaction_coordinator();
  const CoarseTimePoint& deadline = context.GetClientDeadline();
  if (coordinator) {
    VLOG(1) << "Preparing transaction status tablet " << req->tablet_id() << " for deletion.";
    auto status = coordinator->PrepareForDeletion(deadline);
    if (!status.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), status, &context);
      return;
    }
  }
  context.RespondSuccess();
}

void TabletServiceAdminImpl::DeleteTablet(const DeleteTabletRequestPB* req,
                                          DeleteTabletResponsePB* resp,
                                          rpc::RpcContext context) {
  if (PREDICT_FALSE(FLAGS_TEST_rpc_delete_tablet_fail)) {
    context.RespondFailure(STATUS(NetworkError, "Simulating network partition for test"));
    return;
  }

  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "DeleteTablet", req, resp, &context)) {
    return;
  }
  TRACE_EVENT2("tserver", "DeleteTablet",
               "tablet_id", req->tablet_id(),
               "reason", req->reason());

  tablet::TabletDataState delete_type = tablet::TABLET_DATA_UNKNOWN;
  if (req->has_delete_type()) {
    delete_type = req->delete_type();
  }
  LOG(INFO) << "T " << req->tablet_id() << " P " << server_->permanent_uuid()
            << ": Processing DeleteTablet with delete_type " << TabletDataState_Name(delete_type)
            << (req->has_reason() ? (" (" + req->reason() + ")") : "")
            << (req->hide_only() ? " (Hide only)" : "")
            << (req->keep_data() ? " (Not deleting data)" : "")
            << " from " << context.requestor_string();
  VLOG(1) << "Full request: " << req->DebugString();

  boost::optional<int64_t> cas_config_opid_index_less_or_equal;
  if (req->has_cas_config_opid_index_less_or_equal()) {
    cas_config_opid_index_less_or_equal = req->cas_config_opid_index_less_or_equal();
  }
  boost::optional<TabletServerErrorPB::Code> error_code;
  Status s = server_->tablet_manager()->DeleteTablet(
      req->tablet_id(),
      delete_type,
      tablet::ShouldAbortActiveTransactions(req->should_abort_active_txns()),
      cas_config_opid_index_less_or_equal,
      req->hide_only(),
      req->keep_data(),
      &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    HandleErrorResponse(resp, &context, s, error_code);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceAdminImpl::FlushTablets(const FlushTabletsRequestPB* req,
                                          FlushTabletsResponsePB* resp,
                                          rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "FlushTablets", req, resp, &context)) {
    return;
  }

  if (!req->all_tablets() && req->tablet_ids_size() == 0) {
    const Status s = STATUS(InvalidArgument, "No tablet ids");
    SetupErrorAndRespond(resp->mutable_error(), s, &context);
    return;
  }

  server::UpdateClock(*req, server_->Clock());

  TRACE_EVENT1("tserver", "FlushTablets",
               "TS: ", req->dest_uuid());

  LOG_WITH_PREFIX(INFO) << "Processing FlushTablets from " << context.requestor_string();
  VLOG_WITH_PREFIX(1) << "Full FlushTablets request: " << req->DebugString();
  TabletPeers tablet_peers;
  TSTabletManager::TabletPtrs tablet_ptrs;

  if (req->all_tablets()) {
    tablet_peers = server_->tablet_manager()->GetTabletPeers(&tablet_ptrs);
  } else {
    for (const TabletId& id : req->tablet_ids()) {
      auto tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
          server_->tablet_peer_lookup(), id, resp, &context));
      tablet_peers.push_back(std::move(tablet_peer.tablet_peer));
      auto tablet = tablet_peer.tablet;
      if (tablet != nullptr) {
        tablet_ptrs.push_back(std::move(tablet));
      }
    }
  }
  switch (req->operation()) {
    case FlushTabletsRequestPB::FLUSH: {
      auto flush_flags = req->regular_only() ? tablet::FlushFlags::kRegular
                                             : tablet::FlushFlags::kAllDbs;
      VLOG_WITH_PREFIX(1) << "flush_flags: " << to_underlying(flush_flags);
      for (const tablet::TabletPtr& tablet : tablet_ptrs) {
        resp->set_failed_tablet_id(tablet->tablet_id());
        RETURN_UNKNOWN_ERROR_IF_NOT_OK(
            tablet->Flush(tablet::FlushMode::kAsync, flush_flags), resp, &context);
        if (!FLAGS_TEST_skip_force_superblock_flush) {
          RETURN_UNKNOWN_ERROR_IF_NOT_OK(
              tablet->FlushSuperblock(tablet::OnlyIfDirty::kTrue), resp, &context);
        }
        resp->clear_failed_tablet_id();
      }

      // Wait for end of all flush operations.
      for (const tablet::TabletPtr& tablet : tablet_ptrs) {
        resp->set_failed_tablet_id(tablet->tablet_id());
        RETURN_UNKNOWN_ERROR_IF_NOT_OK(tablet->WaitForFlush(), resp, &context);
        resp->clear_failed_tablet_id();
      }
      break;
    }
    case FlushTabletsRequestPB::COMPACT:
      RETURN_UNKNOWN_ERROR_IF_NOT_OK(
          server_->tablet_manager()->TriggerAdminCompaction(tablet_ptrs, true /* should_wait */),
          resp, &context);
      break;
    case FlushTabletsRequestPB::LOG_GC:
      for (const auto& tablet : tablet_peers) {
        resp->set_failed_tablet_id(tablet->tablet_id());
        RETURN_UNKNOWN_ERROR_IF_NOT_OK(tablet->RunLogGC(), resp, &context);
        resp->clear_failed_tablet_id();
      }
      break;
  }

  context.RespondSuccess();
}

void TabletServiceAdminImpl::CountIntents(
    const CountIntentsRequestPB* req,
    CountIntentsResponsePB* resp,
    rpc::RpcContext context) {
  TSTabletManager::TabletPtrs tablet_ptrs;
  TabletPeers tablet_peers = server_->tablet_manager()->GetTabletPeers(&tablet_ptrs);
  int64_t total_intents = 0;
  // TODO: do this in parallel.
  // TODO: per-tablet intent counts.
  for (const auto& tablet : tablet_ptrs) {
    auto num_intents = tablet->CountIntents();
    if (!num_intents.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), num_intents.status(), &context);
      return;
    }
    total_intents += *num_intents;
  }
  resp->set_num_intents(total_intents);
  context.RespondSuccess();
}

void TabletServiceAdminImpl::AddTableToTablet(
    const AddTableToTabletRequestPB* req, AddTableToTabletResponsePB* resp,
    rpc::RpcContext context) {
  auto tablet_id = req->tablet_id();

  const auto tablet =
      LookupLeaderTabletOrRespond(server_->tablet_peer_lookup(), tablet_id, resp, &context);
  if (!tablet) {
    return;
  }
  DVLOG(3) << "Received AddTableToTablet RPC: " << yb::ToString(*req);

  tablet::ChangeMetadataRequestPB change_req;
  *change_req.mutable_add_table() = req->add_table();
  change_req.set_tablet_id(tablet_id);
  Status s = tablet::SyncReplicateChangeMetadataOperation(
      &change_req, tablet.peer.get(), tablet.leader_term);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceAdminImpl::RemoveTableFromTablet(
    const RemoveTableFromTabletRequestPB* req,
    RemoveTableFromTabletResponsePB* resp,
    rpc::RpcContext context) {
  auto tablet =
      LookupLeaderTabletOrRespond(server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  tablet::ChangeMetadataRequestPB change_req;
  change_req.set_remove_table_id(req->remove_table_id());
  change_req.set_tablet_id(req->tablet_id());
  Status s = tablet::SyncReplicateChangeMetadataOperation(
      &change_req, tablet.peer.get(), tablet.leader_term);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceAdminImpl::SplitTablet(
    const tablet::SplitTabletRequestPB* req, SplitTabletResponsePB* resp, rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "SplitTablet", req, resp, &context)) {
    return;
  }
  if (PREDICT_FALSE(FLAGS_TEST_fail_tablet_split_probability > 0) &&
      RandomActWithProbability(FLAGS_TEST_fail_tablet_split_probability)) {
    return SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS(InvalidArgument,  // Use InvalidArgument to hit IsDefinitelyPermanentError().
            "Failing tablet split due to FLAGS_TEST_fail_tablet_split_probability"),
        TabletServerErrorPB::UNKNOWN_ERROR,
        &context);
  }
  TRACE_EVENT1("tserver", "SplitTablet", "tablet_id", req->tablet_id());

  server::UpdateClock(*req, server_->Clock());
  auto leader_tablet_peer =
      LookupLeaderTabletOrRespond(server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!leader_tablet_peer) {
    return;
  }

  {
    auto tablet_data_state = leader_tablet_peer.peer->data_state();
    if (tablet_data_state != tablet::TABLET_DATA_READY) {
      auto s = tablet_data_state == tablet::TABLET_DATA_SPLIT_COMPLETED
                  ? STATUS_FORMAT(AlreadyPresent, "Tablet $0 is already split.", req->tablet_id())
                  : STATUS_FORMAT(
                        InvalidArgument, "Invalid tablet $0 data state: $1", req->tablet_id(),
                        tablet_data_state);
      SetupErrorAndRespond(
          resp->mutable_error(), s, TabletServerErrorPB::TABLET_NOT_RUNNING, &context);
      return;
    }
  }

  const auto consensus_result = leader_tablet_peer.peer->GetConsensus();
  if (!consensus_result) {
    SetupErrorAndRespond(
        resp->mutable_error(), consensus_result.status(), TabletServerErrorPB::TABLET_NOT_RUNNING,
        &context);
    return;
  }

  auto operation = std::make_unique<tablet::SplitOperation>(
      leader_tablet_peer.tablet, server_->tablet_manager());
  *operation->AllocateRequest() = *req;
  operation->mutable_request()->dup_split_parent_leader_uuid(
      leader_tablet_peer.peer->permanent_uuid());

  operation->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));

  leader_tablet_peer.peer->Submit(std::move(operation), leader_tablet_peer.leader_term);
  ts_split_op_added_->Increment();
  LOG(INFO) << leader_tablet_peer.peer->LogPrefix() << "RPC for split tablet successful. "
      << "Submitting request to " << leader_tablet_peer.peer->tablet_id()
      << " term " << leader_tablet_peer.leader_term;
}

void TabletServiceAdminImpl::UpgradeYsql(
    const UpgradeYsqlRequestPB* req,
    UpgradeYsqlResponsePB* resp,
    rpc::RpcContext context) {
  LOG(INFO) << "Starting YSQL upgrade";

  pgwrapper::YsqlUpgradeHelper upgrade_helper(server_->pgsql_proxy_bind_address(),
                                              server_->GetSharedMemoryPostgresAuthKey(),
                                              FLAGS_heartbeat_interval_ms,
                                              req->use_single_connection());
  const auto status = upgrade_helper.Upgrade();
  if (!status.ok()) {
    LOG(INFO) << "YSQL upgrade failed: " << status;
    SetupErrorAndRespond(resp->mutable_error(), status, &context);
    return;
  }

  LOG(INFO) << "YSQL upgrade done successfully";
  context.RespondSuccess();
}

void TabletServiceAdminImpl::WaitForYsqlBackendsCatalogVersion(
    const WaitForYsqlBackendsCatalogVersionRequestPB* req,
    WaitForYsqlBackendsCatalogVersionResponsePB* resp,
    rpc::RpcContext context) {
  VLOG_WITH_PREFIX(2) << "Received Wait for YSQL Backends Catalog Version RPC: "
                      << req->ShortDebugString();

  if (FLAGS_TEST_fail_wait_for_ysql_backends_catalog_version) {
    LOG(INFO) << "Responding with a failure to " << req->ShortDebugString();
    // Send back OPERATION_NOT_SUPPORTED to prevent further retry.
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS(InternalError, "test failure").CloneAndAddErrorCode(
          TabletServerError(TabletServerErrorPB::OPERATION_NOT_SUPPORTED)),
        &context);
    return;
  }

  const PgOid database_oid = req->database_oid();
  const uint64_t catalog_version = req->catalog_version();
  const int prev_num_lagging_backends = req->prev_num_lagging_backends();
  if (prev_num_lagging_backends == 0 || prev_num_lagging_backends < -1) {
    // Send back OPERATION_NOT_SUPPORTED to prevent further retry.
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS_FORMAT(InvalidArgument,
                      "Unexpected prev_num_lagging_backends: $0",
                      prev_num_lagging_backends)
          .CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::OPERATION_NOT_SUPPORTED)),
        &context);
    return;
  }

  const CoarseTimePoint& deadline = context.GetClientDeadline();
  // Reserve 1s for responding back to master.
  const auto modified_deadline = deadline - 1s;

  // First, check tserver's catalog version.
  const std::string db_ver_tag = Format("[DB $0, V $1]", database_oid, catalog_version);
  uint64_t ts_catalog_version = 0;
  SCOPED_WAIT_STATUS(WaitForYsqlBackendsCatalogVersion);
  Status s = Wait(
      [catalog_version, database_oid, this, &ts_catalog_version]() -> Result<bool> {
        // TODO(jason): using the gflag to determine per-db mode may not work for initdb, so make
        // sure to handle that case if initdb ever goes through this codepath.
        if (FLAGS_ysql_enable_db_catalog_version_mode) {
          server_->get_ysql_db_catalog_version(
              database_oid, &ts_catalog_version, nullptr /* last_breaking_catalog_version */);
        } else {
          server_->get_ysql_catalog_version(
              &ts_catalog_version, nullptr /* last_breaking_catalog_version */);
        }
        return ts_catalog_version >= catalog_version;
      },
      modified_deadline,
      Format("Wait for tserver catalog version to reach $0", db_ver_tag));
  if (!s.ok()) {
    DCHECK(s.IsTimedOut());
    const std::string ts_db_ver_tag = Format("[DB $0, V $1]", database_oid, ts_catalog_version);
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS_FORMAT(TryAgain,
                      "Tserver's catalog version is too old: $0 vs $1",
                      ts_db_ver_tag, db_ver_tag),
        &context);
    return;
  }

  // Second, check backends' catalog version.
  // Use template1 database because it is guaranteed to exist.
  // TODO(jason): use database related to database we are waiting on to not overload template1.
  // TODO(jason): come up with a more efficient connection reuse method for tserver-postgres
  // communication.  As of D19621, connections are spawned each request for YSQL upgrade, index
  // backfill, and this.  Creating the connection has a startup cost.
  auto res = pgwrapper::PGConnBuilder({
        .host = PgDeriveSocketDir(server_->pgsql_proxy_bind_address()),
        .port = server_->pgsql_proxy_bind_address().port(),
        .dbname = "template1",
        .user = "postgres",
        .password = UInt64ToString(server_->GetSharedMemoryPostgresAuthKey()),
        .connect_timeout = make_unsigned(std::max(
            2, narrow_cast<int>(ToSeconds(modified_deadline - CoarseMonoClock::Now())))),
      }).Connect();
  if (!res.ok()) {
    LOG_WITH_PREFIX_AND_FUNC(ERROR) << "failed to connect to local postgres: " << res.status();
    SetupErrorAndRespond(resp->mutable_error(), res.status(), &context);
    return;
  }
  pgwrapper::PGConn conn = std::move(*res);

  // TODO(jason): handle or create issue for catalog version being uint64 vs int64.
  const std::string num_lagging_backends_query = Format(
      "SELECT count(*) FROM pg_stat_activity WHERE catalog_version < $0 AND datid = $1",
      catalog_version, database_oid);
  int num_lagging_backends = -1;
  const std::string description = Format("Wait for update to num lagging backends $0", db_ver_tag);
  s = Wait(
      [&]() -> Result<bool> {
        num_lagging_backends = narrow_cast<int>(VERIFY_RESULT(
            conn.FetchRow<pgwrapper::PGUint64>(num_lagging_backends_query)));
        if (num_lagging_backends != prev_num_lagging_backends) {
          SCHECK((prev_num_lagging_backends == -1 ||
                  prev_num_lagging_backends > num_lagging_backends),
                 InternalError,
                 Format("Unexpected prev_num_lagging_backends: $0, num_lagging_backends: $1",
                        prev_num_lagging_backends, num_lagging_backends));
          return true;
        }
        VLOG_WITH_PREFIX(4) << "Some backends are behind: " << num_lagging_backends;
        return false;
      },
      modified_deadline,
      description,
      (prev_num_lagging_backends == -1 ? 10ms : 5s) /* initial_delay */,
      1.4 /* delay_multiplier */,
      5s /* max_delay */);

  if (s.IsTimedOut() && s.message().ToBuffer().find(description) != std::string::npos) {
    LOG_WITH_PREFIX(INFO) << "Deadline reached: still waiting on " << num_lagging_backends
                          << " backends " << db_ver_tag;
  } else if (!s.ok()) {
    LOG_WITH_PREFIX_AND_FUNC(ERROR) << "num lagging backends query failed: " << s;
    SetupErrorAndRespond(resp->mutable_error(), s, &context);
    return;
  }
  DCHECK_GE(num_lagging_backends, 0);
  resp->set_num_lagging_backends(num_lagging_backends);
  context.RespondSuccess();
}

void TabletServiceAdminImpl::UpdateTransactionTablesVersion(
    const UpdateTransactionTablesVersionRequestPB* req,
    UpdateTransactionTablesVersionResponsePB* resp,
    rpc::RpcContext context) {
  LOG(INFO) << "Received update in transaction tables version to version " << req->version();

  auto context_ptr = std::make_shared<rpc::RpcContext>(std::move(context));
  auto callback = [resp, context_ptr](const Status& status) {
    if (!status.ok()) {
      LOG(WARNING) << status;
      SetupErrorAndRespond(resp->mutable_error(), status, context_ptr.get());
    } else {
      context_ptr->RespondSuccess();
    }
  };

  server_->TransactionManager().UpdateTransactionTablesVersion(req->version(), callback);
}


bool EmptyWriteBatch(const docdb::KeyValueWriteBatchPB& write_batch) {
  return write_batch.write_pairs().empty() && write_batch.apply_external_transactions().empty();
}

Status TabletServiceImpl::PerformWrite(
    const WriteRequestPB* req, WriteResponsePB* resp, rpc::RpcContext* context) {
  if (req->include_trace()) {
    context->EnsureTraceCreated();
  }
  ADOPT_TRACE(context->trace());
  TRACE("Start Write");
  TRACE_EVENT1("tserver", "TabletServiceImpl::Write",
               "tablet_id", req->tablet_id());
  VLOG(2) << "Received Write RPC: " << req->DebugString();
  UpdateClock(*req, server_->Clock());

  auto tablet = VERIFY_RESULT(LookupLeaderTablet(server_->tablet_peer_lookup(), req->tablet_id()));
  RETURN_NOT_OK(CheckWriteThrottling(req->rejection_score(), tablet.peer.get()));

  if (tablet.tablet->metadata()->hidden()) {
    return STATUS(
        NotFound, "Tablet not found", req->tablet_id(),
        TabletServerError(TabletServerErrorPB::TABLET_NOT_FOUND));
  }

#if defined(DUMP_WRITE)
  if (req->has_write_batch() && req->write_batch().has_transaction()) {
    VLOG(1) << "Write with transaction: " << req->write_batch().transaction().ShortDebugString();
    if (req->pgsql_write_batch_size() != 0) {
      auto txn_id = CHECK_RESULT(FullyDecodeTransactionId(
          req->write_batch().transaction().transaction_id()));
      for (const auto& entry : req->pgsql_write_batch()) {
        if (entry.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPDATE) {
          auto key = entry.column_new_values(0).expr().value().int32_value();
          LOG(INFO) << txn_id << " UPDATE: " << key << " = "
                    << entry.column_new_values(1).expr().value().string_value();
        } else if (
            entry.stmt_type() == PgsqlWriteRequestPB::PGSQL_INSERT ||
            entry.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPSERT) {
          dockv::DocKey doc_key;
          CHECK_OK(doc_key.FullyDecodeFrom(entry.ybctid_column_value().value().binary_value()));
          LOG(INFO) << txn_id << " INSERT: " << doc_key.hashed_group()[0].GetInt32() << " = "
                    << entry.column_values(0).expr().value().string_value();
        } else if (entry.stmt_type() == PgsqlWriteRequestPB::PGSQL_DELETE) {
          LOG(INFO) << txn_id << " DELETE: " << entry.ShortDebugString();
        }
      }
    }
  }
#endif

  if (PREDICT_FALSE(req->has_write_batch() && !req->has_external_hybrid_time() &&
      (!req->write_batch().write_pairs().empty() || !req->write_batch().read_pairs().empty()))) {
    return STATUS(
        NotSupported, "Write Request contains write batch. This field should be "
        "used only for post-processed write requests during Raft replication.",
        TabletServerError(TabletServerErrorPB::INVALID_MUTATION));
  }

  bool has_operations = req->ql_write_batch_size() != 0 ||
                        req->redis_write_batch_size() != 0 ||
                        req->pgsql_write_batch_size() != 0 ||
                        (req->has_external_hybrid_time() && !EmptyWriteBatch(req->write_batch()));
  if (!has_operations && tablet.tablet->table_type() != TableType::REDIS_TABLE_TYPE) {
    // An empty request. This is fine, can just exit early with ok status instead of working hard.
    // This doesn't need to go to Raft log.
    MakeRpcOperationCompletionCallback(std::move(*context), resp, server_->Clock())(Status::OK());
    return Status::OK();
  }

  // For postgres requests check that the syscatalog version matches.
  if (tablet.tablet->table_type() == TableType::PGSQL_TABLE_TYPE) {
    CatalogVersionChecker catalog_version_checker(*server_);
    for (const auto& pg_req : req->pgsql_write_batch()) {
      RETURN_NOT_OK(catalog_version_checker(pg_req));
    }
  }

  auto context_ptr = std::make_shared<RpcContext>(std::move(*context));

  const auto leader_term = req->has_leader_term() && req->leader_term() != OpId::kUnknownTerm
                               ? req->leader_term()
                               : tablet.leader_term;

  auto query = std::make_unique<tablet::WriteQuery>(
      leader_term, context_ptr->GetClientDeadline(), tablet.peer.get(), tablet.tablet,
      context_ptr.get(), resp);
  query->set_client_request(*req);

  if (RandomActWithProbability(GetAtomicFlag(&FLAGS_TEST_respond_write_failed_probability))) {
    LOG(INFO) << "Responding with a failure to " << req->DebugString();
    tablet.peer->WriteAsync(std::move(query));
    auto status = STATUS(LeaderHasNoLease, "TEST: Random failure");
    SetupErrorAndRespond(resp->mutable_error(), std::move(status), context_ptr.get());
    return Status::OK();
  }

  query->set_callback(WriteQueryCompletionCallback(
      tablet.peer, context_ptr, resp, query.get(), server_->Clock(), req->include_trace(),
      req->has_leader_term()));

  query->AdjustYsqlQueryTransactionality(req->pgsql_write_batch_size());

  tablet.peer->WriteAsync(std::move(query));

  return Status::OK();
}

void TabletServiceImpl::Write(const WriteRequestPB* req,
                              WriteResponsePB* resp,
                              rpc::RpcContext context) {
  if (FLAGS_TEST_tserver_noop_read_write) {
    for ([[maybe_unused]] const auto& batch : req->ql_write_batch()) {
      resp->add_ql_response_batch();
    }
    context.RespondSuccess();
    return;
  }

  const auto& wait_state = ash::WaitStateInfo::CurrentWaitState();
  if (wait_state && req->has_tablet_id()) {
    wait_state->UpdateAuxInfo(ash::AshAuxInfo{.tablet_id = req->tablet_id(), .method = "Write"});
  }
  if (wait_state && req->has_ash_metadata()) {
    wait_state->UpdateMetadataFromPB(req->ash_metadata());
  }
  auto status = PerformWrite(req, resp, &context);
  if (!status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), std::move(status), &context);
  }
}

void TabletServiceImpl::Read(const ReadRequestPB* req,
                             ReadResponsePB* resp,
                             rpc::RpcContext context) {
#ifndef NDEBUG
  if (PREDICT_FALSE(FLAGS_TEST_wait_row_mark_exclusive_count > 0)) {
    for (const auto& pgsql_req : req->pgsql_batch()) {
      if (pgsql_req.has_row_mark_type() &&
          pgsql_req.row_mark_type() == RowMarkType::ROW_MARK_EXCLUSIVE) {
        static CountDownLatch row_mark_exclusive_latch(FLAGS_TEST_wait_row_mark_exclusive_count);
        row_mark_exclusive_latch.CountDown();
        row_mark_exclusive_latch.Wait();
        DEBUG_ONLY_TEST_SYNC_POINT("TabletServiceImpl::Read::RowMarkExclusive:1");
        DEBUG_ONLY_TEST_SYNC_POINT("TabletServiceImpl::Read::RowMarkExclusive:2");
        break;
      }
    }
  }
#endif // NDEBUG

  if (FLAGS_TEST_tserver_noop_read_write) {
    context.RespondSuccess();
    return;
  }

  const auto& wait_state = ash::WaitStateInfo::CurrentWaitState();
  if (wait_state && req->has_tablet_id()) {
    wait_state->UpdateAuxInfo(ash::AshAuxInfo{.tablet_id = req->tablet_id(), .method = "Read"});
  }
  if (wait_state && req->has_ash_metadata()) {
    wait_state->UpdateMetadataFromPB(req->ash_metadata());
  }
  PerformRead(server_, this, req, resp, std::move(context));
}

ConsensusServiceImpl::ConsensusServiceImpl(const scoped_refptr<MetricEntity>& metric_entity,
                                           TabletPeerLookupIf* tablet_manager)
    : ConsensusServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
}

ConsensusServiceImpl::~ConsensusServiceImpl() {
}

void ConsensusServiceImpl::CompleteUpdateConsensusResponse(
    std::shared_ptr<tablet::TabletPeer> tablet_peer,
    consensus::LWConsensusResponsePB* resp) {
  auto tablet = tablet_peer->shared_tablet();
  if (tablet) {
    resp->set_num_sst_files(tablet->GetCurrentVersionNumSSTFiles());
  }
  resp->set_propagated_hybrid_time(tablet_peer->clock().Now().ToUint64());
}

void ConsensusServiceImpl::MultiRaftUpdateConsensus(
      const consensus::MultiRaftConsensusRequestPB *req,
      consensus::MultiRaftConsensusResponsePB *resp,
      rpc::RpcContext context) {
    DVLOG(3) << "Received Batch Consensus Update RPC: " << req->ShortDebugString();
    // Effectively performs ConsensusServiceImpl::UpdateConsensus for
    // each ConsensusRequestPB in the batch but does not fail the entire
    // batch if a single request fails.
    for (int i = 0; i < req->consensus_request_size(); i++) {
      // Unfortunately, we have to use const_cast here,
      // because the protobuf-generated interface only gives us a const request
      // but we need to be able to move messages out of the request for efficiency.
      auto consensus_req = const_cast<ConsensusRequestPB*>(&req->consensus_request(i));
      auto consensus_resp = resp->add_consensus_response();;

      auto uuid_match_res = CheckUuidMatch(tablet_manager_, "UpdateConsensus", consensus_req,
                                           context.requestor_string());
      if (!uuid_match_res.ok()) {
        SetupError(consensus_resp->mutable_error(), uuid_match_res.status());
        continue;
      }

      auto peer_tablet_res = LookupTabletPeer(tablet_manager_, consensus_req->tablet_id());
      if (!peer_tablet_res.ok()) {
        SetupError(consensus_resp->mutable_error(), peer_tablet_res.status());
        continue;
      }
      auto tablet_peer = peer_tablet_res.get().tablet_peer;

      // Submit the update directly to the TabletPeer's Consensus instance.
      auto consensus_res = GetConsensus(tablet_peer);
      if (!consensus_res.ok()) {
        SetupError(consensus_resp->mutable_error(), consensus_res.status());
        continue;
      }
      auto consensus = *consensus_res;

      // TODO(lw_uc) effective update for multiraft.
      auto temp_resp = rpc::MakeSharedMessage<consensus::LWConsensusResponsePB>();
      Status s = consensus->Update(
         rpc::CopySharedMessage(*consensus_req),
         temp_resp.get(), context.GetClientDeadline());
      if (PREDICT_FALSE(!s.ok())) {
        // Clear the response first, since a partially-filled response could
        // result in confusing a caller, or in having missing required fields
        // in embedded optional messages.
        consensus_resp->Clear();
        SetupError(consensus_resp->mutable_error(), s);
        continue;
      }

      CompleteUpdateConsensusResponse(tablet_peer, temp_resp.get());
      temp_resp->ToGoogleProtobuf(consensus_resp);
    }
    context.RespondSuccess();
}

void ConsensusServiceImpl::UpdateConsensus(const consensus::LWConsensusRequestPB* req,
                                           consensus::LWConsensusResponsePB* resp,
                                           rpc::RpcContext context) {
  DVLOG(3) << "Received Consensus Update RPC: " << req->ShortDebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "UpdateConsensus", req, resp, &context)) {
    return;
  }
  auto peer_tablet = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));
  auto tablet_peer = peer_tablet.tablet_peer;

  // Submit the update directly to the TabletPeer's Consensus instance.
  shared_ptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;

  // Unfortunately, we have to use const_cast here, because the protobuf-generated interface only
  // gives us a const request, but we need to be able to move messages out of the request for
  // efficiency.
  Status s = consensus->Update(
      rpc::SharedField(context.shared_params(), const_cast<consensus::LWConsensusRequestPB*>(req)),
      resp, context.GetClientDeadline());
  if (PREDICT_FALSE(!s.ok())) {
    // Clear the response first, since a partially-filled response could
    // result in confusing a caller, or in having missing required fields
    // in embedded optional messages.
    resp->Clear();

    SetupErrorAndRespond(resp->mutable_error(), s, &context);
    return;
  }

  CompleteUpdateConsensusResponse(tablet_peer, resp);

  auto trace = Trace::CurrentTrace();
  if (trace && req->trace_requested()) {
    resp->dup_trace_buffer(trace->DumpToString(true));
  }
  context.RespondSuccess();
}

void ConsensusServiceImpl::RequestConsensusVote(const VoteRequestPB* req,
                                                VoteResponsePB* resp,
                                                rpc::RpcContext context) {
  DVLOG(3) << "Received Consensus Request Vote RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "RequestConsensusVote", req, resp, &context)) {
    return;
  }
  auto peer_tablet = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));
  auto tablet_peer = peer_tablet.tablet_peer;

  // Submit the vote request directly to the consensus instance.
  shared_ptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;
  Status s = consensus->RequestVote(req, resp);
  RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, &context);
  context.RespondSuccess();
}

void ConsensusServiceImpl::ChangeConfig(const ChangeConfigRequestPB* req,
                                        ChangeConfigResponsePB* resp,
                                        RpcContext context) {
  VLOG(1) << "Received ChangeConfig RPC: " << req->ShortDebugString();
  // If the destination uuid is empty string, it means the client was retrying after a leader
  // stepdown and did not have a chance to update the uuid inside the request.
  // TODO: Note that this can be removed once Java YBClient will reset change config's uuid
  // correctly after leader step down.
  if (req->dest_uuid() != "" &&
      !CheckUuidMatchOrRespond(tablet_manager_, "ChangeConfig", req, resp, &context)) {
    return;
  }
  auto peer_tablet = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));
  auto tablet_peer = peer_tablet.tablet_peer;

  shared_ptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;
  boost::optional<TabletServerErrorPB::Code> error_code;
  std::shared_ptr<RpcContext> context_ptr = std::make_shared<RpcContext>(std::move(context));
  Status s = consensus->ChangeConfig(*req, BindHandleResponse(resp, context_ptr), &error_code);
  VLOG(1) << "Sent ChangeConfig req " << req->ShortDebugString() << " to consensus layer.";
  if (PREDICT_FALSE(!s.ok())) {
    HandleErrorResponse(resp, context_ptr.get(), s, error_code);
    return;
  }
  // The success case is handled when the callback fires.
}

void ConsensusServiceImpl::UnsafeChangeConfig(const UnsafeChangeConfigRequestPB* req,
                                              UnsafeChangeConfigResponsePB* resp,
                                              RpcContext context) {
  VLOG(1) << "Received UnsafeChangeConfig RPC: " << req->ShortDebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "UnsafeChangeConfig", req, resp, &context)) {
    return;
  }
  auto peer_tablet = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));
  auto tablet_peer = peer_tablet.tablet_peer;

  shared_ptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) {
    return;
  }
  boost::optional<TabletServerErrorPB::Code> error_code;
  const Status s = consensus->UnsafeChangeConfig(*req, &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, &context);
    HandleErrorResponse(resp, &context, s, error_code);
    return;
  }
  context.RespondSuccess();
}

void ConsensusServiceImpl::GetNodeInstance(const GetNodeInstanceRequestPB* req,
                                           GetNodeInstanceResponsePB* resp,
                                           rpc::RpcContext context) {
  DVLOG(3) << "Received Get Node Instance RPC: " << req->DebugString();
  resp->mutable_node_instance()->CopyFrom(tablet_manager_->NodeInstance());
  auto status = tablet_manager_->GetRegistration(resp->mutable_registration());
  if (!status.ok()) {
    context.RespondFailure(status);
  } else {
    context.RespondSuccess();
  }
}

namespace {

class NODISCARD_CLASS RpcScope {
 public:
  template<class Req, class Resp>
  RpcScope(TabletPeerLookupIf* tablet_manager,
           const char* method_name,
           const Req* req,
           Resp* resp,
           rpc::RpcContext* context)
      : context_(context) {
    if (!CheckUuidMatchOrRespond(tablet_manager, method_name, req, resp, context)) {
      return;
    }
    auto peer_tablet = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
        tablet_manager, req->tablet_id(), resp, context));
    auto tablet_peer = peer_tablet.tablet_peer;

    if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus_)) {
      return;
    }
    responded_ = false;
  }

  ~RpcScope() {
    if (!responded_) {
      context_->RespondSuccess();
    }
  }

  template<class Resp>
  void CheckStatus(const Status& status, Resp* resp) {
    if (!status.ok()) {
      LOG(INFO) << "Status failed: " << status.ToString();
      SetupErrorAndRespond(resp->mutable_error(), status, context_);
      responded_ = true;
    }
  }

  Consensus* operator->() {
    return consensus_.get();
  }

  explicit operator bool() const {
    return !responded_;
  }

 private:
  rpc::RpcContext* context_;
  bool responded_ = true;
  shared_ptr<Consensus> consensus_;
};

} // namespace

void ConsensusServiceImpl::RunLeaderElection(const RunLeaderElectionRequestPB* req,
                                             RunLeaderElectionResponsePB* resp,
                                             rpc::RpcContext context) {
  VLOG(1) << "Received Run Leader Election RPC: " << req->DebugString();
  RpcScope scope(tablet_manager_, "RunLeaderElection", req, resp, &context);
  if (!scope) {
    return;
  }

  Status s = scope->StartElection(consensus::LeaderElectionData {
    .mode = consensus::ElectionMode::ELECT_EVEN_IF_LEADER_IS_ALIVE,
    .pending_commit = req->has_committed_index(),
    .must_be_committed_opid = OpId::FromPB(req->committed_index()),
    .originator_uuid = req->has_originator_uuid() ? req->originator_uuid() : std::string(),
    .suppress_vote_request = consensus::TEST_SuppressVoteRequest(req->suppress_vote_request()),
    .initial_election = req->initial_election() });
  scope.CheckStatus(s, resp);
}

void ConsensusServiceImpl::LeaderElectionLost(const consensus::LeaderElectionLostRequestPB *req,
                                              consensus::LeaderElectionLostResponsePB *resp,
                                              ::yb::rpc::RpcContext context) {
  LOG(INFO) << "LeaderElectionLost, req: " << req->ShortDebugString();
  RpcScope scope(tablet_manager_, "LeaderElectionLost", req, resp, &context);
  if (!scope) {
    return;
  }
  auto status = scope->ElectionLostByProtege(req->election_lost_by_uuid());
  scope.CheckStatus(status, resp);
  LOG(INFO) << "LeaderElectionLost, outcome: " << (scope ? "success" : "failure") << "req: "
            << req->ShortDebugString();
}

void ConsensusServiceImpl::LeaderStepDown(const LeaderStepDownRequestPB* req,
                                          LeaderStepDownResponsePB* resp,
                                          RpcContext context) {
  LOG(INFO) << "Received Leader stepdown RPC: " << req->ShortDebugString();

  if (PREDICT_FALSE(FLAGS_TEST_leader_stepdown_delay_ms > 0)) {
    LOG(INFO) << "Delaying leader stepdown for "
              << FLAGS_TEST_leader_stepdown_delay_ms << " ms.";
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_leader_stepdown_delay_ms));
  }

  RpcScope scope(tablet_manager_, "LeaderStepDown", req, resp, &context);
  if (!scope) {
    return;
  }
  Status s = scope->StepDown(req, resp);
  if (!resp->has_error()) {
    LOG(INFO) << "Leader stepdown request " << req->ShortDebugString() << " failed. Resp code="
              << TabletServerErrorPB::Code_Name(resp->error().code());
  } else {
    LOG(INFO) << "Leader stepdown request " << req->ShortDebugString() << " succeeded";
  }
  scope.CheckStatus(s, resp);
}

void ConsensusServiceImpl::GetLastOpId(const consensus::GetLastOpIdRequestPB *req,
                                       consensus::GetLastOpIdResponsePB *resp,
                                       rpc::RpcContext context) {
  DVLOG(3) << "Received GetLastOpId RPC: " << req->DebugString();

  if (PREDICT_FALSE(req->opid_type() == consensus::UNKNOWN_OPID_TYPE)) {
    HandleErrorResponse(resp, &context,
                        STATUS(InvalidArgument, "Invalid opid_type specified to GetLastOpId()"));
    return;
  }

  if (!CheckUuidMatchOrRespond(tablet_manager_, "GetLastOpId", req, resp, &context)) {
    return;
  }
  auto peer_tablet = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));
  auto tablet_peer = peer_tablet.tablet_peer;

  if (tablet_peer->state() != tablet::RUNNING) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(ServiceUnavailable, "Tablet Peer not in RUNNING state"),
                         TabletServerErrorPB::TABLET_NOT_RUNNING, &context);
    return;
  }

  auto consensus = GetConsensusOrRespond(tablet_peer, resp, &context);
  if (!consensus) return;
  auto op_id = req->has_op_type()
      ? consensus->TEST_GetLastOpIdWithType(req->opid_type(), req->op_type())
      : consensus->GetLastOpId(req->opid_type());

  // RETURN_UNKNOWN_ERROR_IF_NOT_OK does not support Result, so have to add extra check here.
  if (!op_id.ok()) {
    RETURN_UNKNOWN_ERROR_IF_NOT_OK(op_id.status(), resp, &context);
  }
  op_id->ToPB(resp->mutable_opid());
  context.RespondSuccess();
}

void ConsensusServiceImpl::GetConsensusState(const consensus::GetConsensusStateRequestPB *req,
                                             consensus::GetConsensusStateResponsePB *resp,
                                             rpc::RpcContext context) {
  DVLOG(3) << "Received GetConsensusState RPC: " << req->DebugString();

  RpcScope scope(tablet_manager_, "GetConsensusState", req, resp, &context);
  if (!scope) {
    return;
  }
  ConsensusConfigType type = req->type();
  if (PREDICT_FALSE(type != CONSENSUS_CONFIG_ACTIVE && type != CONSENSUS_CONFIG_COMMITTED)) {
    HandleErrorResponse(resp, &context,
        STATUS(InvalidArgument, Substitute("Unsupported ConsensusConfigType $0 ($1)",
                                           ConsensusConfigType_Name(type), type)));
    return;
  }
  LeaderLeaseStatus leader_lease_status;
  *resp->mutable_cstate() = scope->ConsensusState(req->type(), &leader_lease_status);
  resp->set_leader_lease_status(leader_lease_status);
}

void ConsensusServiceImpl::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB* req,
                                                StartRemoteBootstrapResponsePB* resp,
                                                rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(tablet_manager_, "StartRemoteBootstrap", req, resp, &context)) {
    return;
  }
  if (req->has_split_parent_tablet_id()
      && !PREDICT_FALSE(FLAGS_TEST_disable_post_split_tablet_rbs_check)) {
    // For any tablet that was the result of a split, the raft group leader will always send the
    // split_parent_tablet_id. However, our local tablet manager should only know about the parent
    // if it was part of the raft group which committed the split to the parent, and if the parent
    // tablet has yet to be deleted across the cluster.
    auto tablet_peer = tablet_manager_->GetServingTablet(req->split_parent_tablet_id());
    if (tablet_peer.ok()) {
      auto tablet = (**tablet_peer).shared_tablet();
      // If local parent tablet replica has been already split or remote bootstrapped from remote
      // replica that has been already split - allow RBS of child tablets.
      // In this case we can't rely on local parent tablet replica split to create child tablet
      // replicas on the current node, because local bootstrap is not replaying already applied
      // SPLIT_OP (it has op_id <= flushed_op_id).
      if (!tablet || tablet->metadata()->tablet_data_state() !=
                         tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
        YB_LOG_EVERY_N_SECS(WARNING, 30)
            << "Start remote bootstrap rejected: parent tablet not yet split.";
        SetupErrorAndRespond(
            resp->mutable_error(),
            STATUS(Incomplete, "Rejecting bootstrap request while parent tablet is present."),
            TabletServerErrorPB::TABLET_SPLIT_PARENT_STILL_LIVE,
            &context);
        return;
      }
    }
  }

  Status s = tablet_manager_->StartRemoteBootstrap(*req);
  if (!s.ok()) {
    // Using Status::AlreadyPresent for a remote bootstrap operation that is already in progress.
    if (s.IsAlreadyPresent()) {
      YB_LOG_EVERY_N_SECS(WARNING, 30) << "Start remote bootstrap failed: " << s;
      SetupErrorAndRespond(resp->mutable_error(), s, TabletServerErrorPB::ALREADY_IN_PROGRESS,
                           &context);
      return;
    } else {
      LOG(WARNING) << "Start remote bootstrap failed: " << s;
    }
  }

  RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, &context);
  context.RespondSuccess();
}

void TabletServiceImpl::NoOp(const NoOpRequestPB *req,
                             NoOpResponsePB *resp,
                             rpc::RpcContext context) {
  context.RespondSuccess();
}

void TabletServiceImpl::Publish(
    const PublishRequestPB* req, PublishResponsePB* resp, rpc::RpcContext context) {
  rpc::Publisher* publisher = server_->GetPublisher();
  resp->set_num_clients_forwarded_to(publisher ? (*publisher)(req->channel(), req->message()) : 0);
  context.RespondSuccess();
}

void TabletServiceImpl::ListTablets(const ListTabletsRequestPB* req,
                                    ListTabletsResponsePB* resp,
                                    rpc::RpcContext context) {
  TabletPeers peers = server_->tablet_manager()->GetTabletPeers();
  RepeatedPtrField<StatusAndSchemaPB>* peer_status = resp->mutable_status_and_schema();
  for (const TabletPeerPtr& peer : peers) {
    StatusAndSchemaPB* status = peer_status->Add();
    peer->GetTabletStatusPB(status->mutable_tablet_status());
    SchemaToPB(*peer->status_listener()->schema(), status->mutable_schema());
    peer->tablet_metadata()->partition_schema()->ToPB(status->mutable_partition_schema());
  }
  context.RespondSuccess();
}

void TabletServiceImpl::GetMasterAddresses(const GetMasterAddressesRequestPB* req,
                                           GetMasterAddressesResponsePB* resp,
                                           rpc::RpcContext context) {
  resp->set_master_addresses(server::MasterAddressesToString(
      *server_->tablet_manager()->server()->options().GetMasterAddresses()));
  context.RespondSuccess();
}

void TabletServiceImpl::GetLogLocation(
    const GetLogLocationRequestPB* req,
    GetLogLocationResponsePB* resp,
    rpc::RpcContext context) {
  resp->set_log_location(FLAGS_log_dir);
  context.RespondSuccess();
}

void TabletServiceImpl::ListTabletsForTabletServer(const ListTabletsForTabletServerRequestPB* req,
                                                   ListTabletsForTabletServerResponsePB* resp,
                                                   rpc::RpcContext context) {
  // Replicating logic from path-handlers.
  TabletPeers peers = server_->tablet_manager()->GetTabletPeers();
  for (const TabletPeerPtr& peer : peers) {
    TabletStatusPB status;
    peer->GetTabletStatusPB(&status);

    ListTabletsForTabletServerResponsePB::Entry* data_entry = resp->add_entries();
    data_entry->set_table_name(status.table_name());
    data_entry->set_tablet_id(status.tablet_id());

    auto consensus_result = peer->GetConsensus();
    data_entry->set_is_leader(
        consensus_result && consensus_result.get()->role() == PeerRole::LEADER);
    data_entry->set_state(status.state());

    auto tablet = peer->shared_tablet();
    uint64_t num_sst_files = tablet ? tablet->GetCurrentVersionNumSSTFiles() : 0;
    data_entry->set_num_sst_files(num_sst_files);

    uint64_t num_log_segments = peer->GetNumLogSegments();
    data_entry->set_num_log_segments(num_log_segments);

    auto num_memtables = tablet ? tablet->GetNumMemtables() : std::make_pair(0, 0);
    data_entry->set_num_memtables_intents(num_memtables.first);
    data_entry->set_num_memtables_regular(num_memtables.second);
  }

  context.RespondSuccess();
}

namespace {

Result<uint64_t> CalcChecksum(tablet::Tablet* tablet, CoarseTimePoint deadline) {
  auto scoped_read_operation = tablet->CreateScopedRWOperationNotBlockingRocksDbShutdownStart();
  RETURN_NOT_OK(scoped_read_operation);

  const shared_ptr<Schema> schema = tablet->metadata()->schema();
  dockv::ReaderProjection projection(*schema);
  auto iter = tablet->NewRowIterator(projection, {}, "", deadline);
  RETURN_NOT_OK(iter);

  qlexpr::QLTableRow value_map;
  ScanResultChecksummer collector;

  while (VERIFY_RESULT((**iter).FetchNext(&value_map))) {
    collector.HandleRow(*schema, value_map);
  }

  return collector.agg_checksum();
}

} // namespace

Result<uint64_t> TabletServiceImpl::DoChecksum(
    const ChecksumRequestPB* req, CoarseTimePoint deadline) {
  auto abstract_tablet = VERIFY_RESULT(GetTablet(
      server_->tablet_peer_lookup(), req->tablet_id(), /* tablet_peer = */ nullptr,
      req->consistency_level(), AllowSplitTablet::kTrue));
  return CalcChecksum(down_cast<tablet::Tablet*>(abstract_tablet.get()), deadline);
}

void TabletServiceImpl::Checksum(const ChecksumRequestPB* req,
                                 ChecksumResponsePB* resp,
                                 rpc::RpcContext context) {
  VLOG(1) << "Full request: " << req->DebugString();

  auto checksum = DoChecksum(req, context.GetClientDeadline());
  if (!checksum.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), checksum.status(), &context);
    return;
  }

  resp->set_checksum(*checksum);
  context.RespondSuccess();
}

void TabletServiceImpl::ImportData(const ImportDataRequestPB* req,
                                   ImportDataResponsePB* resp,
                                   rpc::RpcContext context) {
  auto peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context));

  auto status = peer.tablet->ImportData(req->source_dir());
  if (!status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), status, &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceImpl::GetTabletStatus(const GetTabletStatusRequestPB* req,
                                        GetTabletStatusResponsePB* resp,
                                        rpc::RpcContext context) {
  const Status s = server_->GetTabletStatus(req, resp);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         s.IsNotFound() ? TabletServerErrorPB::TABLET_NOT_FOUND
                                        : TabletServerErrorPB::UNKNOWN_ERROR,
                         &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceImpl::IsTabletServerReady(const IsTabletServerReadyRequestPB* req,
                                            IsTabletServerReadyResponsePB* resp,
                                            rpc::RpcContext context) {
  Status s = server_->tablet_manager()->GetNumTabletsPendingBootstrap(resp);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), s, &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceImpl::GetSplitKey(
    const GetSplitKeyRequestPB* req, GetSplitKeyResponsePB* resp, RpcContext context) {
  TEST_PAUSE_IF_FLAG(TEST_pause_tserver_get_split_key);
  PerformAtLeader(req, resp, &context,
      [req, resp](const LeaderTabletPeer& leader_tablet_peer) -> Status {
        const auto& tablet = leader_tablet_peer.tablet;
        if (!req->is_manual_split() &&
            FLAGS_rocksdb_max_file_size_for_compaction > 0 &&
            tablet->schema()->table_properties().HasDefaultTimeToLive()) {
          auto s = STATUS(NotSupported, "Tablet splitting not supported for TTL tables.");
          return s.CloneAndAddErrorCode(
              TabletServerError(TabletServerErrorPB::TABLET_SPLIT_DISABLED_TTL_EXPIRY));
        }
        if (tablet->MayHaveOrphanedPostSplitData()) {
          return STATUS(IllegalState, "Tablet has orphaned post-split data");
        }
        std::string partition_split_hash_key;
        const auto split_encoded_key =
            VERIFY_RESULT(tablet->GetEncodedMiddleSplitKey(&partition_split_hash_key));
        resp->set_split_encoded_key(split_encoded_key);
        resp->set_split_partition_key(partition_split_hash_key.size() ? partition_split_hash_key
                                                                      : split_encoded_key);
        return Status::OK();
  });
}

void TabletServiceImpl::GetSharedData(const GetSharedDataRequestPB* req,
                                      GetSharedDataResponsePB* resp,
                                      rpc::RpcContext context) {
  auto& data = server_->SharedObject();
  resp->mutable_data()->assign(pointer_cast<const char*>(&data), sizeof(data));
  context.RespondSuccess();
}

void TabletServiceImpl::GetTserverCatalogVersionInfo(
    const GetTserverCatalogVersionInfoRequestPB* req,
    GetTserverCatalogVersionInfoResponsePB* resp,
    rpc::RpcContext context) {
  auto status = server_->get_ysql_db_oid_to_cat_version_info_map(*req, resp);
  if (!status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), status, &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceImpl::ListMasterServers(const ListMasterServersRequestPB* req,
                                          ListMasterServersResponsePB* resp,
                                          rpc::RpcContext context) {
  const Status s = server_->tablet_manager()->server()->ListMasterServers(req, resp);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceImpl::CheckTserverTabletHealth(const CheckTserverTabletHealthRequestPB* req,
                                                CheckTserverTabletHealthResponsePB* resp,
                                                rpc::RpcContext context) {
  TRACE("CheckTserverTabletHealth");
  for (auto& tablet_id : req->tablet_ids()) {
    auto res = LookupTabletPeer(server_->tablet_manager(), tablet_id);
    if (!res.ok()) {
      LOG_WITH_FUNC(INFO) << "Failed lookup for tablet " << tablet_id;
      continue;
    }

    auto consensus = GetConsensusOrRespond(res->tablet_peer, resp, &context);
    if (!consensus) {
      LOG_WITH_FUNC(INFO) << "Could not find consensus for tablet " << tablet_id;
      continue;
    }

    auto* tablet_health = resp->add_tablet_healths();
    tablet_health->set_tablet_id(tablet_id);

    auto role = consensus->role();
    tablet_health->set_role(role);

    if (FLAGS_TEST_set_tablet_follower_lag_ms != 0) {
      tablet_health->set_follower_lag_ms(FLAGS_TEST_set_tablet_follower_lag_ms);
      continue;
    }
    if (role != PeerRole::LEADER) {
      tablet_health->set_follower_lag_ms(consensus->follower_lag_ms());
    }
  }
  TEST_PAUSE_IF_FLAG(TEST_pause_before_tablet_health_response);
  context.RespondSuccess();
}

void TabletServiceImpl::GetLockStatus(const GetLockStatusRequestPB* req,
                                      GetLockStatusResponsePB* resp,
                                      rpc::RpcContext context) {
  TRACE("GetLockStatus");

  // Only one among transactions_by_tablet and transaction_ids should be set.
  if (req->transactions_by_tablet().empty() == (req->transaction_ids_size() == 0)) {
    auto s = STATUS(
        IllegalState,
        "Request must specify either txns_by_tablet or txn_ids, and cannot specify both.");
    LOG(DFATAL) << s;
    SetupErrorAndRespond(resp->mutable_error(), s, &context);
    return;
  }

  TabletPeers tablet_peers;
  std::map<TransactionId, SubtxnSet> limit_resp_to_txns;
  for (const auto& [tablet_id, _] : req->transactions_by_tablet()) {
    // GetLockStatusRequestPB may include tablets that aren't hosted at this tablet server.
    auto res = LookupTabletPeer(server_->tablet_manager(), tablet_id);
    if (!res.ok()) {
      continue;
    }
    tablet_peers.push_back(res->tablet_peer);
  }

  if (req->transaction_ids().size() > 0) {
    // If this request specifies transaction_ids, then we check every tablet leader at this tserver
    // TODO(pglocks): We should have involved tablet info in this case as well. See
    // https://github.com/yugabyte/yugabyte-db/issues/16913
    tablet_peers = server_->tablet_manager()->GetTabletPeers();
  }
  for (auto& txn_id : req->transaction_ids()) {
    auto id_or_status = FullyDecodeTransactionId(txn_id);
    if (!id_or_status.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), id_or_status.status(), &context);
      return;
    }
    // TODO(pglocks): Include aborted_subtxn info here as well.
    limit_resp_to_txns.emplace(std::make_pair(*id_or_status, SubtxnSet()));
  }

  for (const auto& tablet_peer : tablet_peers) {
    auto leader_term = tablet_peer->LeaderTerm();
    if (leader_term != OpId::kUnknownTerm &&
        tablet_peer->tablet_metadata()->table_type() == PGSQL_TABLE_TYPE) {
      const auto& tablet_id = tablet_peer->tablet_id();
      auto* tablet_lock_info = resp->add_tablet_lock_infos();
      Status s = Status::OK();
      if (req->transactions_by_tablet().count(tablet_id) > 0) {
        std::map<TransactionId, SubtxnSet> transactions;
        for (auto& txn : req->transactions_by_tablet().at(tablet_id).transactions()) {
          auto& txn_id = txn.id();
          auto id_or_status = FullyDecodeTransactionId(txn_id);
          if (!id_or_status.ok()) {
            resp->Clear();
            SetupErrorAndRespond(resp->mutable_error(), id_or_status.status(), &context);
            return;
          }
          auto aborted_subtxns_or_status = SubtxnSet::FromPB(txn.aborted().set());
          if (!aborted_subtxns_or_status.ok()) {
            resp->Clear();
            SetupErrorAndRespond(
                resp->mutable_error(), aborted_subtxns_or_status.status(), &context);
            return;
          }
          transactions.emplace(std::make_pair(*id_or_status, *aborted_subtxns_or_status));
        }
        s = tablet_peer->shared_tablet()->GetLockStatus(transactions, tablet_lock_info);
      } else {
        DCHECK(!limit_resp_to_txns.empty());
        s = tablet_peer->shared_tablet()->GetLockStatus(limit_resp_to_txns, tablet_lock_info);
      }
      if (!s.ok()) {
        resp->Clear();
        SetupErrorAndRespond(resp->mutable_error(), s, &context);
        return;
      }
      tablet_lock_info->set_term(leader_term);
    }
  }
  context.RespondSuccess();
}

void TabletServiceImpl::CancelTransaction(
    const CancelTransactionRequestPB* req, CancelTransactionResponsePB* resp,
    rpc::RpcContext context) {
  TRACE("CancelTransaction");

  auto id_or_status = FullyDecodeTransactionId(req->transaction_id());
  if (!id_or_status.ok()) {
    return SetupErrorAndRespond(resp->mutable_error(), id_or_status.status(), &context);
  }
  const auto& txn_id = *id_or_status;

  TabletPeers status_tablet_peers;
  if (req->status_tablet_id().empty()) {
    status_tablet_peers = server_->tablet_manager()->GetStatusTabletPeers();
  } else {
    auto peer_or_status = LookupTabletPeerOrRespond(
        server_->tablet_manager(), req->status_tablet_id(), resp, &context);
    if (!peer_or_status.ok()) {
      return;
    }
    // Ensure that the given tablet is a status tablet and that the tablet peer is initialized.
    auto peer = peer_or_status->tablet_peer;
    const auto& tablet_ptr = peer->shared_tablet();
    if (!tablet_ptr || !tablet_ptr->transaction_coordinator()) {
      return SetupErrorAndRespond(resp->mutable_error(),
                                  STATUS_FORMAT(IllegalState,
                                                "Transaction Coordinator not found for tablet $0",
                                                req->status_tablet_id()),
                                  &context);
    }
    status_tablet_peers.push_back(std::move(peer));
  }

  std::vector<std::future<Result<TransactionStatusResult>>> txn_status_res_futures;
  for (const auto& tablet_peer : status_tablet_peers) {
    // If the peer is not the leader for the group,
    // 1. and the cancel request contains a status tablet, return a NOT_LEADER error.
    // 2. and the cancel request does not contain a status tablet id, skip the peer.
    //
    // We do not return an error in case of 2. as we need to broadcast the cancel request to all
    // status tablets. If we return an error instead, we might miss looking at the leader peer
    // of the transaction status tablet, and hence would not cancel the transaction.
    auto res = LeaderTerm(*tablet_peer);
    if (!res.ok()) {
      if (!req->status_tablet_id().empty()) {
        return SetupErrorAndRespond(resp->mutable_error(), res.status(), &context);
      }
      continue;
    }

    auto leader_term = *res;
    auto txn_found = false;
    auto tablet_ptr = tablet_peer->shared_tablet();
    auto future = MakeFuture<Result<TransactionStatusResult>>(
        [txn_id, tablet_peer, leader_term, tablet_ptr, &txn_found](auto callback) {
      txn_found = tablet_ptr->transaction_coordinator()->CancelTransactionIfFound(
          txn_id,
          leader_term,
          [callback, tablet_peer] (Result<TransactionStatusResult> res) {
            // Note: If the txn is successfully canceled, we need not validate the peer's
            // leadership status as we see TransactionStatus::ABORTED only after the ABORT
            // operation is replicated across all involved tablets. So we can forward the
            // result immaterial of the peer's leadership status.
            callback(res);
          });
    });

    if (txn_found) {
      txn_status_res_futures.push_back(std::move(future));
    }
  }

  // Return if the transaction is not hosted on any of the status tablets of this TabletServer.
  if (txn_status_res_futures.empty()) {
    return SetupErrorAndRespond(resp->mutable_error(),
                                STATUS_FORMAT(NotFound,
                                              "Failed canceling transaction $0", txn_id.ToString()),
                                &context);
  }

  // There should be at most 1 status tablet hosting the transaction. In case of transaction
  // promotion, the transaction can be present at 2 status tablets until commit time.
  DCHECK_LE(txn_status_res_futures.size(), 2);

  for (auto& future : txn_status_res_futures) {
    // Errors and txn statuses other than ABORTED take precedence over TransactionStatus::ABORTED.
    // This needs to be done to correctly handle cancelation requests of promoted transactions.
    const auto& res = future.get();
    if (!res.ok() || res->status != TransactionStatus::ABORTED) {
      auto s = !res.ok() ? res.status() : STATUS_FORMAT(NotSupported,
                                                        "Cannot cancel transaction $0 in state $1",
                                                        txn_id.ToString(),
                                                        res->ToString());
      return SetupErrorAndRespond(resp->mutable_error(), s, &context);
    }
    DCHECK_EQ(res->status, TransactionStatus::ABORTED);
  }

  context.RespondSuccess();
}

void TabletServiceImpl::StartRemoteSnapshotTransfer(
    const StartRemoteSnapshotTransferRequestPB* req, StartRemoteSnapshotTransferResponsePB* resp,
    rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(
          server_->tablet_manager(), "StartRemoteSnapshotTransfer", req, resp, &context)) {
    return;
  }

  Status s = server_->tablet_manager()->StartRemoteSnapshotTransfer(*req);
  if (!s.ok()) {
    // Using Status::AlreadyPresent for a remote snapshot transfer operation that is already in
    // progress.
    if (s.IsAlreadyPresent()) {
      YB_LOG_EVERY_N_SECS(WARNING, 30) << "Start remote snapshot transfer failed: " << s;
      SetupErrorAndRespond(
          resp->mutable_error(), s, TabletServerErrorPB::ALREADY_IN_PROGRESS, &context);
      return;
    } else {
      LOG(WARNING) << "Start remote snapshot transfer failed: " << s;
    }
  }

  RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, &context);
  context.RespondSuccess();
}

void TabletServiceImpl::GetTabletKeyRanges(
    const GetTabletKeyRangesRequestPB* req, GetTabletKeyRangesResponsePB* resp,
    rpc::RpcContext context) {
  PerformAtLeader(
      req, resp, &context,
      [req, &context](const LeaderTabletPeer& leader_tablet_peer) -> Status {
        const auto& tablet = leader_tablet_peer.tablet;
        RETURN_NOT_OK(tablet->GetTabletKeyRanges(
            req->lower_bound_key(), req->upper_bound_key(), req->max_num_ranges(),
            req->range_size_bytes(), tablet::IsForward(req->is_forward()), req->max_key_length(),
            &context.sidecars().Start()));
        return Status::OK();
      });
}

void TabletServiceAdminImpl::TestRetry(
    const TestRetryRequestPB* req, TestRetryResponsePB* resp, rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "TestRetry", req, resp, &context)) {
    return;
  }
  auto num_calls = TEST_num_test_retry_calls_.fetch_add(1) + 1;
  if (num_calls < req->num_retries()) {
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS_FORMAT(TryAgain, "Got $0 calls of $1", num_calls, req->num_retries()), &context);
  } else {
    context.RespondSuccess();
  }
}

void TabletServiceImpl::Shutdown() {
}

}  // namespace tserver
}  // namespace yb
