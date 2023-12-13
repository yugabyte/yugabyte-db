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

#include "yb/cdc/cdc_service.h"

#include <algorithm>
#include <chrono>
#include <memory>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/cdc/cdc_producer.h"
#include "yb/cdc/cdc_util.h"
#include "yb/cdc/xcluster_rpc.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_service_context.h"
#include "yb/cdc/cdc_state_table.h"
#include "yb/cdc/cdc_types.h"
#include "yb/cdc/xcluster_producer_bootstrap.h"
#include "yb/cdc/xrepl_stream_metadata.h"
#include "yb/cdc/xrepl_stream_stats.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"

#include "yb/common/entity_ids.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/replicate_msgs_holder.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/strings/join.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"

#include "yb/rocksdb/rate_limiter.h"
#include "yb/rocksdb/util/rate_limiter.h"

#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/debug-util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/service_util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stol_utils.h"
#include "yb/util/stopwatch.h"
#include "yb/util/sync_point.h"
#include "yb/util/thread.h"
#include "yb/util/trace.h"

#include "yb/yql/cql/ql/util/statement_result.h"

using std::max;
using std::min;
using std::pair;
using std::string;
using std::vector;

constexpr uint32_t kUpdateIntervalMs = 15 * 1000;

DEFINE_UNKNOWN_int32(cdc_read_rpc_timeout_ms, 30 * 1000,
    "Timeout used for CDC read rpc calls.  Reads normally occur cross-cluster.");
TAG_FLAG(cdc_read_rpc_timeout_ms, advanced);

DEFINE_UNKNOWN_int32(cdc_write_rpc_timeout_ms, 30 * 1000,
    "Timeout used for CDC write rpc calls.  Writes normally occur intra-cluster.");
TAG_FLAG(cdc_write_rpc_timeout_ms, advanced);

DEPRECATE_FLAG(int32, cdc_ybclient_reactor_threads, "09_2023");

DEFINE_UNKNOWN_int32(cdc_state_checkpoint_update_interval_ms, kUpdateIntervalMs,
    "Rate at which CDC state's checkpoint is updated.");

DEFINE_UNKNOWN_string(certs_for_cdc_dir, "",
    "The parent directory of where all certificates for xCluster producer universes will "
    "be stored, for when the producer and consumer clusters use different certificates. "
    "Place the certificates for each producer cluster in "
    "<certs_for_cdc_dir>/<producer_cluster_id>/*.");

DEFINE_UNKNOWN_int32(update_min_cdc_indices_interval_secs, 60,
    "How often to read cdc_state table to get the minimum applied index for each tablet "
    "across all streams. This information is used to correctly keep log files that "
    "contain unapplied entries. This is also the rate at which a tablet's minimum "
    "replicated index across all streams is sent to the other peers in the configuration. "
    "If flag enable_log_retention_by_op_idx is disabled, this flag has no effect.");

DEFINE_RUNTIME_int32(update_metrics_interval_ms, kUpdateIntervalMs,
    "How often to update xDC cluster metrics.");

// enable_cdc_client_tablet_caching is disabled because cdc code does notify the meta cache when
// requests to the peers fail. Also, meta cache does not handle addition of peers, which can cause
// issues with cdc checkpoint updates.
DEFINE_RUNTIME_bool(enable_cdc_client_tablet_caching, false,
    "Enable caching the tablets found by client.");

DEFINE_RUNTIME_bool(enable_collect_cdc_metrics, true, "Enable collecting cdc metrics.");

DEFINE_UNKNOWN_double(cdc_read_safe_deadline_ratio, .10,
    "When the heartbeat deadline has this percentage of time remaining, "
    "the master should halt tablet report processing so it can respond in time.");

DEFINE_UNKNOWN_double(cdc_get_changes_free_rpc_ratio, .10,
    "When the TServer only has this percentage of RPCs remaining because the rest are "
    "GetChanges, reject additional requests to throttle/backoff and prevent deadlocks.");

DEFINE_UNKNOWN_bool(enable_update_local_peer_min_index, true,
    "Enable each local peer to update its own log checkpoint instead of the leader "
    "updating all peers.");

DEPRECATE_FLAG(bool, parallelize_bootstrap_producer, "08_2023");

DEFINE_test_flag(uint64, cdc_log_init_failure_timeout_seconds, 0,
    "Timeout in seconds for CDCServiceImpl::SetCDCCheckpoint to return log init failure");

DEFINE_UNKNOWN_int32(wait_replication_drain_tserver_max_retry, 3,
    "Maximum number of retry that a tserver will poll its tablets until the tablets"
    "are all caught-up in the replication, before responding to the caller.");

DEFINE_UNKNOWN_int32(wait_replication_drain_tserver_retry_interval_ms, 100,
    "Time in microseconds that a tserver will sleep between each iteration of polling "
    "its tablets until the tablets are all caught-up in the replication.");

DEFINE_test_flag(bool, block_get_changes, false,
    "For testing only. When set to true, GetChanges will not send any new changes "
    "to the consumer.");

DEFINE_test_flag(bool, cdc_inject_replication_index_update_failure, false,
    "Injects an error after updating a tablet's replication index entry");

DEFINE_test_flag(bool, force_get_checkpoint_from_cdc_state, false,
    "Always bypass the cache and fetch the checkpoint from the cdc state table");

DEFINE_RUNTIME_int32(xcluster_get_changes_max_send_rate_mbps, 100,
                     "Server-wide max send rate in megabytes per second for GetChanges response "
                     "traffic. Throttles xcluster but not cdc traffic.");

DEFINE_RUNTIME_bool(enable_xcluster_stat_collection, true,
    "When enabled, stats are collected from xcluster streams for reporting purposes.");

DECLARE_bool(enable_log_retention_by_op_idx);

DECLARE_int32(cdc_checkpoint_opid_interval_ms);

DECLARE_int32(rpc_workers_limit);

DECLARE_int64(cdc_intent_retention_ms);
DECLARE_bool(enable_xcluster_auto_flag_validation);

DECLARE_bool(TEST_ysql_yb_enable_replication_commands);

METRIC_DEFINE_entity(cdc);

METRIC_DEFINE_entity(cdcsdk);

#define VERIFY_STRING_TO_STREAM_ID(stream_id_str) \
  VERIFY_RESULT_OR_SET_CODE( \
      xrepl::StreamId::FromString(stream_id_str), CDCError(CDCErrorPB::INVALID_REQUEST))

#define RPC_VERIFY_STRING_TO_STREAM_ID(stream_id_str) \
  RPC_VERIFY_RESULT( \
      xrepl::StreamId::FromString(stream_id_str), resp->mutable_error(), \
      CDCErrorPB::INVALID_REQUEST, context)

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace cdc {

using client::internal::RemoteTabletServer;
using rpc::RpcContext;

constexpr int kMaxDurationForTabletLookup = 50;

MonoTime test_expire_time_cdc_log_init_failure = MonoTime::kUninitialized;

namespace {

// These are guarded by lock_.
// Map of checkpoints that have been sent to CDC consumer and stored in cdc_state.
struct TabletCheckpointInfo {
 public:
  ProducerTabletInfo producer_tablet_info;

  mutable TabletCheckpoint cdc_state_checkpoint;
  mutable TabletCheckpoint sent_checkpoint;
  mutable MemTrackerPtr mem_tracker;

  const TabletId& tablet_id() const { return producer_tablet_info.tablet_id; }

  const xrepl::StreamId& stream_id() const { return producer_tablet_info.stream_id; }
};

struct CDCStateMetadataInfo {
  ProducerTabletInfo producer_tablet_info;

  mutable uint64_t commit_timestamp;
  mutable OpId last_streamed_op_id;
  mutable SchemaDetailsMap schema_details_map;

  std::shared_ptr<MemTracker> mem_tracker;

  const TableId& tablet_id() const { return producer_tablet_info.tablet_id; }

  const xrepl::StreamId& stream_id() const { return producer_tablet_info.stream_id; }
};

class TabletTag;
class StreamTag;

using TabletCheckpoints = boost::multi_index_container<
    TabletCheckpointInfo,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<boost::multi_index::member<
            TabletCheckpointInfo, ProducerTabletInfo, &TabletCheckpointInfo::producer_tablet_info>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<TabletTag>,
            boost::multi_index::const_mem_fun<
                TabletCheckpointInfo, const TabletId&, &TabletCheckpointInfo::tablet_id>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<StreamTag>,
            boost::multi_index::const_mem_fun<
                TabletCheckpointInfo, const xrepl::StreamId&, &TabletCheckpointInfo::stream_id>>>>;

using CDCStateMetadata = boost::multi_index_container<
    CDCStateMetadataInfo,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<boost::multi_index::member<
            CDCStateMetadataInfo, ProducerTabletInfo, &CDCStateMetadataInfo::producer_tablet_info>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<TabletTag>,
            boost::multi_index::const_mem_fun<
                CDCStateMetadataInfo, const TabletId&, &CDCStateMetadataInfo::tablet_id>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<StreamTag>,
            boost::multi_index::const_mem_fun<
                CDCStateMetadataInfo, const xrepl::StreamId&, &CDCStateMetadataInfo::stream_id>>>>;

}  // namespace

class CDCServiceImpl::Impl {
 public:
  explicit Impl(CDCServiceContext* context, rw_spinlock* mutex)
      : async_client_init_(context->MakeClientInitializer(
            "cdc_client", std::chrono::milliseconds(FLAGS_cdc_read_rpc_timeout_ms))),
        mutex_(*mutex) {
    async_client_init_->Start();
  }

  void UpdateCDCStateMetadata(
      const ProducerTabletInfo& producer_tablet,
      const uint64_t& timestamp,
      const SchemaDetailsMap& schema_details,
      const OpId& op_id) {
    std::lock_guard l(mutex_);
    auto it = cdc_state_metadata_.find(producer_tablet);
    if (it == cdc_state_metadata_.end()) {
      LOG(DFATAL) << "Failed to update the cdc state metadata for tablet id: "
                  << producer_tablet.tablet_id;
      return;
    }
    it->commit_timestamp = timestamp;
    it->last_streamed_op_id = op_id;
    it->schema_details_map = schema_details;
  }

  SchemaDetailsMap& GetOrAddSchema(
      const ProducerTabletInfo& producer_tablet, const bool need_schema_info) {
    std::lock_guard l(mutex_);
    auto it = cdc_state_metadata_.find(producer_tablet);

    if (it != cdc_state_metadata_.end()) {
      if (need_schema_info) {
        it->schema_details_map.clear();
      }
      return it->schema_details_map;
    }
    CDCStateMetadataInfo info = CDCStateMetadataInfo{
        .producer_tablet_info = producer_tablet,
        .commit_timestamp = {},
        .last_streamed_op_id = OpId::Invalid(),
        .schema_details_map = {},
        .mem_tracker = nullptr};
    cdc_state_metadata_.emplace(info);
    it = cdc_state_metadata_.find(producer_tablet);
    return it->schema_details_map;
  }

  boost::optional<OpId> GetLastStreamedOpId(const ProducerTabletInfo& producer_tablet) {
    SharedLock<rw_spinlock> lock(mutex_);
    auto it = cdc_state_metadata_.find(producer_tablet);
    if (it != cdc_state_metadata_.end()) {
      return it->last_streamed_op_id;
    }
    return boost::none;
  }

  void AddTabletCheckpoint(
      OpId op_id, const xrepl::StreamId& stream_id, const TabletId& tablet_id) {
    ProducerTabletInfo producer_tablet{
        .replication_group_id = {}, .stream_id = stream_id, .tablet_id = tablet_id};
    CoarseTimePoint time = CoarseMonoClock::Now();
    int64_t active_time = GetCurrentTimeMicros();

    std::lock_guard l(mutex_);
    if (!tablet_checkpoints_.count(producer_tablet)) {
      tablet_checkpoints_.emplace(TabletCheckpointInfo{
          .producer_tablet_info = producer_tablet,
          .cdc_state_checkpoint = {op_id, time, active_time},
          .sent_checkpoint = {op_id, time, active_time},
          .mem_tracker = nullptr});
    }
  }

  void EraseStreams(
      const std::vector<ProducerTabletInfo>& producer_entries_modified,
      bool erase_cdc_states) NO_THREAD_SAFETY_ANALYSIS {
    for (const auto& entry : producer_entries_modified) {
      tablet_checkpoints_.get<StreamTag>().erase(entry.stream_id);
      if (erase_cdc_states) {
        cdc_state_metadata_.get<StreamTag>().erase(entry.stream_id);
      }
    }
  }

  boost::optional<int64_t> GetLastActiveTime(const ProducerTabletInfo& producer_tablet) {
    SharedLock<rw_spinlock> lock(mutex_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      // Use last_active_time from cache only if it is current.
      if (it->cdc_state_checkpoint.last_active_time > 0) {
        if (!it->cdc_state_checkpoint.ExpiredAt(
                FLAGS_cdc_state_checkpoint_update_interval_ms * 1ms, CoarseMonoClock::Now())) {
          VLOG(2) << "Found recent entry in cache with active time: "
                  << it->cdc_state_checkpoint.last_active_time
                  << ", for tablet: " << producer_tablet.tablet_id
                  << ", and stream: " << producer_tablet.stream_id;
          return it->cdc_state_checkpoint.last_active_time;
        } else {
          VLOG(2) << "Found stale entry in cache with active time: "
                  << it->cdc_state_checkpoint.last_active_time
                  << ", for tablet: " << producer_tablet.tablet_id
                  << ", and stream: " << producer_tablet.stream_id
                  << ". We will read from the cdc_state table";
        }
      }
    } else {
      VLOG(1) << "Did not find entry in 'tablet_checkpoints_' cache for tablet: "
              << producer_tablet.tablet_id << ", stream: " << producer_tablet.stream_id;
    }

    return boost::none;
  }

  Status EraseTabletAndStreamEntry(const ProducerTabletInfo& info) {
    std::lock_guard l(mutex_);
    // Here we just remove the entries of the tablet from the in-memory caches. The deletion from
    // the 'cdc_state' table will happen when the hidden parent tablet will be deleted
    // asynchronously.
    tablet_checkpoints_.erase(info);
    cdc_state_metadata_.erase(info);

    return Status::OK();
  }

  boost::optional<OpId> GetLastCheckpoint(const ProducerTabletInfo& producer_tablet) {
    SharedLock<rw_spinlock> lock(mutex_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      // Use checkpoint from cache only if it is current.
      if (it->cdc_state_checkpoint.op_id.index > 0 &&
          !it->cdc_state_checkpoint.ExpiredAt(
              FLAGS_cdc_state_checkpoint_update_interval_ms * 1ms, CoarseMonoClock::Now())) {
        return it->cdc_state_checkpoint.op_id;
      }
    }
    return boost::none;
  }

  bool UpdateCheckpoint(
      const ProducerTabletInfo& producer_tablet, const OpId& sent_op_id, const OpId& commit_op_id) {
    VLOG(1) << "T " << producer_tablet.tablet_id << " going to update the checkpoint with "
            << commit_op_id;
    auto now = CoarseMonoClock::Now();
    auto active_time = GetCurrentTimeMicros();

    TabletCheckpoint sent_checkpoint = {
        .op_id = sent_op_id,
        .last_update_time = now,
        .last_active_time = active_time,
    };
    TabletCheckpoint commit_checkpoint = {
        .op_id = commit_op_id,
        .last_update_time = now,
        .last_active_time = active_time,
    };

    std::lock_guard l(mutex_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      it->sent_checkpoint = sent_checkpoint;

      if (commit_op_id.index >= 0) {
        it->cdc_state_checkpoint.op_id = commit_op_id;
      }

      // Check if we need to update cdc_state table.
      if (!it->cdc_state_checkpoint.ExpiredAt(
              FLAGS_cdc_state_checkpoint_update_interval_ms * 1ms, now)) {
        return false;
      }

      it->cdc_state_checkpoint.last_update_time = now;
    } else {
      tablet_checkpoints_.emplace(TabletCheckpointInfo{
          .producer_tablet_info = producer_tablet,
          .cdc_state_checkpoint = commit_checkpoint,
          .sent_checkpoint = sent_checkpoint,
          .mem_tracker = nullptr,
      });
    }

    return true;
  }

  OpId GetMinSentCheckpointForTablet(const TabletId& tablet_id) {
    OpId min_op_id = OpId::Max();

    SharedLock<rw_spinlock> l(mutex_);
    auto it_range = tablet_checkpoints_.get<TabletTag>().equal_range(tablet_id);
    if (it_range.first == it_range.second) {
      LOG(WARNING) << "Tablet ID not found in stream_tablets map: " << tablet_id;
      return min_op_id;
    }

    auto cdc_checkpoint_opid_interval = FLAGS_cdc_checkpoint_opid_interval_ms * 1ms;
    for (auto it = it_range.first; it != it_range.second; ++it) {
      // We don't want to include streams that are not being actively polled.
      // So, if the stream has not been polled in the last x seconds,
      // then we ignore that stream while calculating min op ID.
      if (!it->sent_checkpoint.ExpiredAt(cdc_checkpoint_opid_interval, CoarseMonoClock::Now()) &&
          it->sent_checkpoint.op_id.index < min_op_id.index) {
        min_op_id = it->sent_checkpoint.op_id;
      }
    }
    return min_op_id;
  }

  MemTrackerPtr GetMemTracker(
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
      const ProducerTabletInfo& producer_info) {
    {
      SharedLock<rw_spinlock> l(mutex_);
      auto it = tablet_checkpoints_.find(producer_info);
      if (it == tablet_checkpoints_.end()) {
        return nullptr;
      }
      if (it->mem_tracker) {
        return it->mem_tracker;
      }
    }
    std::lock_guard l(mutex_);
    auto it = tablet_checkpoints_.find(producer_info);
    if (it == tablet_checkpoints_.end()) {
      return nullptr;
    }
    if (it->mem_tracker) {
      return it->mem_tracker;
    }
    auto tablet_ptr = tablet_peer->shared_tablet();
    if (!tablet_ptr) {
      return nullptr;
    }
    auto cdc_mem_tracker = MemTracker::FindOrCreateTracker(
        "CDC", tablet_ptr->mem_tracker());
    it->mem_tracker =
        MemTracker::FindOrCreateTracker(producer_info.stream_id.ToString(), cdc_mem_tracker);
    return it->mem_tracker;
  }

  Result<bool> PreCheckTabletValidForStream(const ProducerTabletInfo& info) {
    SharedLock<rw_spinlock> l(mutex_);
    if (tablet_checkpoints_.count(info) != 0) {
      return true;
    }

    if (tablet_checkpoints_.get<StreamTag>().count(info.stream_id) != 0) {
      // Did not find matching tablet ID.
      LOG(INFO) << "Tablet ID " << info.tablet_id << " is not part of stream ID " << info.stream_id
                << ". Repopulating tablet list for this stream.";
    }
    return false;
  }

  Status AddEntriesForChildrenTabletsOnSplitOp(
      const ProducerTabletInfo& info,
      const std::array<TabletId, 2>& tablets,
      const OpId& children_op_id) {
    std::lock_guard l(mutex_);

    for (const auto& tablet : tablets) {
      ProducerTabletInfo producer_info{
          info.replication_group_id, info.stream_id, tablet};
      tablet_checkpoints_.emplace(TabletCheckpointInfo{
          .producer_tablet_info = producer_info,
          .cdc_state_checkpoint =
              TabletCheckpoint{
                  .op_id = children_op_id, .last_update_time = {}, .last_active_time = {}},
          .sent_checkpoint =
              TabletCheckpoint{
                  .op_id = children_op_id, .last_update_time = {}, .last_active_time = {}},
          .mem_tracker = nullptr,
      });
      cdc_state_metadata_.emplace(CDCStateMetadataInfo{
          .producer_tablet_info = producer_info,
          .commit_timestamp = {},
          .last_streamed_op_id = children_op_id,
          .schema_details_map = {},
          .mem_tracker = nullptr,
      });
    }

    return Status::OK();
  }

  Status CheckTabletValidForStream(
      const ProducerTabletInfo& info,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
    bool found = false;
    {
      std::lock_guard l(mutex_);
      for (const auto& tablet : tablets) {
        // Add every tablet in the stream.
        ProducerTabletInfo producer_info{
            info.replication_group_id, info.stream_id, tablet.tablet_id()};
        tablet_checkpoints_.emplace(TabletCheckpointInfo{
            .producer_tablet_info = producer_info,
            .cdc_state_checkpoint =
                TabletCheckpoint{.op_id = {}, .last_update_time = {}, .last_active_time = {}},
            .sent_checkpoint =
                TabletCheckpoint{.op_id = {}, .last_update_time = {}, .last_active_time = {}},
            .mem_tracker = nullptr,
        });
        cdc_state_metadata_.emplace(CDCStateMetadataInfo{
            .producer_tablet_info = producer_info,
            .commit_timestamp = {},
            .last_streamed_op_id = OpId::Invalid(),
            .schema_details_map = {},
            .mem_tracker = nullptr,
        });
        // If this is the tablet that the user requested.
        if (tablet.tablet_id() == info.tablet_id) {
          found = true;
        }
      }
    }
    return found ? Status::OK()
                 : STATUS_FORMAT(
                       InvalidArgument, "Tablet ID $0 is not part of stream ID $1", info.tablet_id,
                       info.stream_id);
  }

  boost::optional<OpId> MinOpId(const TabletId& tablet_id) {
    boost::optional<OpId> result;
    SharedLock<rw_spinlock> l(mutex_);
    // right => multimap where keys are tablet_ids and values are stream_ids.
    // left => multimap where keys are stream_ids and values are tablet_ids.
    auto it_range = tablet_checkpoints_.get<TabletTag>().equal_range(tablet_id);
    if (it_range.first != it_range.second) {
      // Iterate over all the streams for this tablet.
      for (auto it = it_range.first; it != it_range.second; ++it) {
        if (!result || it->cdc_state_checkpoint.op_id.index < result->index) {
          result = it->cdc_state_checkpoint.op_id;
        }
      }
    } else {
      VLOG(2) << "Didn't find any streams for tablet " << tablet_id;
    }

    return result;
  }

  TabletCheckpoints TabletCheckpointsCopy() {
    SharedLock<rw_spinlock> lock(mutex_);
    return tablet_checkpoints_;
  }

  Result<TabletCheckpoint> TEST_GetTabletInfoFromCache(const ProducerTabletInfo& producer_tablet) {
    SharedLock<rw_spinlock> l(mutex_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      return it->cdc_state_checkpoint;
    }
    return STATUS_FORMAT(
        InternalError, "Tablet info: $0 not found in cache.", producer_tablet.ToString());
  }

  void UpdateActiveTime(const ProducerTabletInfo& producer_tablet) {
    SharedLock<rw_spinlock> l(mutex_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      auto active_time = GetCurrentTimeMicros();
      VLOG(2) << "Updating active time for tablet: " << producer_tablet.tablet_id
              << ", stream: " << producer_tablet.stream_id << ", as: " << active_time
              << ", previous value: " << it->cdc_state_checkpoint.last_active_time;
      it->cdc_state_checkpoint.last_active_time = active_time;
    }
  }

  void ForceCdcStateUpdate(const ProducerTabletInfo& producer_tablet) {
    std::lock_guard l(mutex_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      // Setting the timestamp to min will result in ExpiredAt saying it is expired.
      it->cdc_state_checkpoint.last_update_time = CoarseTimePoint::min();
    }
  }

  void ClearCaches() {
    std::lock_guard l(mutex_);
    tablet_checkpoints_.clear();
    cdc_state_metadata_.clear();
  }

  std::unique_ptr<client::AsyncClientInitialiser> async_client_init_;

  // this will be used for the std::call_once call while caching the client
  std::once_flag is_client_cached_;

 private:
  rw_spinlock& mutex_;

  TabletCheckpoints tablet_checkpoints_ GUARDED_BY(mutex_);

  CDCStateMetadata cdc_state_metadata_ GUARDED_BY(mutex_);
};

CDCServiceImpl::CDCServiceImpl(
    std::unique_ptr<CDCServiceContext> context,
    const scoped_refptr<MetricEntity>& metric_entity_server,
    MetricRegistry* metric_registry)
    : CDCServiceIf(metric_entity_server),
      context_(std::move(context)),
      metric_registry_(metric_registry),
      server_metrics_(std::make_shared<CDCServerMetrics>(metric_entity_server)),
      get_changes_rpc_sem_(std::max(
          1.0, floor(FLAGS_rpc_workers_limit * (1 - FLAGS_cdc_get_changes_free_rpc_ratio)))),
      rate_limiter_(std::unique_ptr<rocksdb::RateLimiter>(rocksdb::NewGenericRateLimiter(
          GetAtomicFlag(&FLAGS_xcluster_get_changes_max_send_rate_mbps) * 1_MB))),
      impl_(new Impl(context_.get(), &mutex_)) {
  cdc_state_table_ = std::make_unique<cdc::CDCStateTable>(impl_->async_client_init_.get());

  CHECK_OK(Thread::Create(
      "cdc_service", "update_peers_and_metrics", &CDCServiceImpl::UpdatePeersAndMetrics, this,
      &update_peers_and_metrics_thread_));

  rate_limiter_->EnableLoggingWithDescription("CDC Service");

  LOG_IF(WARNING, get_changes_rpc_sem_.GetValue() == 1) << "only 1 thread available for GetChanges";
}

CDCServiceImpl::~CDCServiceImpl() { Shutdown(); }

client::YBClient* CDCServiceImpl::client() { return impl_->async_client_init_->client(); }

namespace {

bool YsqlTableHasPrimaryKey(const client::YBSchema& schema) {
  for (const auto& col : schema.columns()) {
    if (col.order() == static_cast<int32_t>(PgSystemAttrNum::kYBRowId)) {
      // ybrowid column is added for tables that don't have user-specified primary key.
      return false;
    }
  }
  return true;
}

std::unordered_map<std::string, std::string> GetCreateCDCStreamOptions(
    const CreateCDCStreamRequestPB* req) {
  std::unordered_map<std::string, std::string> options;
  if (req->has_namespace_name()) {
    options.reserve(5);
  } else {
    options.reserve(4);
  }

  options.emplace(kRecordType, CDCRecordType_Name(req->record_type()));
  options.emplace(kRecordFormat, CDCRecordFormat_Name(req->record_format()));
  options.emplace(kSourceType, CDCRequestSource_Name(req->source_type()));
  options.emplace(kCheckpointType, CDCCheckpointType_Name(req->checkpoint_type()));
  if (req->has_namespace_name()) {
    options.emplace(kIdType, kNamespaceId);
  }

  return options;
}

Status DoUpdateCDCConsumerOpId(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const OpId& checkpoint,
    const TabletId& tablet_id) {
  VERIFY_RESULT(tablet_peer->GetConsensus())->UpdateCDCConsumerOpId(checkpoint);
  return Status::OK();
}

bool UpdateCheckpointRequired(
    const StreamMetadata& record, const CDCSDKCheckpointPB& cdc_sdk_op_id, bool* is_snapshot) {
  *is_snapshot = false;
  switch (record.GetSourceType()) {
    case XCLUSTER:
      return true;

    case CDCSDK:
      if (cdc_sdk_op_id.write_id() == 0) {
        return true;
      }
      if (CDCServiceImpl::IsCDCSDKSnapshotRequest(cdc_sdk_op_id)) {
        *is_snapshot = true;
        // CDC should update the stream active time in cdc_state table, during snapshot operation to
        // avoid stream expiry.
        return true;
      }
      break;

    default:
      return false;
  }
  return false;
}

bool GetExplicitOpIdAndSafeTime(
    const GetChangesRequestPB* req, OpId* op_id, CDCSDKCheckpointPB* cdc_sdk_explicit_op_id,
    uint64_t* cdc_sdk_explicit_safe_time) {
  if (req->has_explicit_cdc_sdk_checkpoint()) {
    *cdc_sdk_explicit_op_id = req->explicit_cdc_sdk_checkpoint();
    *op_id = OpId::FromPB(*cdc_sdk_explicit_op_id);
    if (req->explicit_cdc_sdk_checkpoint().has_snapshot_time()) {
      *cdc_sdk_explicit_safe_time = 0;
    } else {
      *cdc_sdk_explicit_safe_time = req->explicit_cdc_sdk_checkpoint().snapshot_time();
    }
    return true;
  }

  return false;
}

bool GetFromOpId(const GetChangesRequestPB* req, OpId* op_id, CDCSDKCheckpointPB* cdc_sdk_op_id) {
  if (req->has_from_checkpoint()) {
    *op_id = OpId::FromPB(req->from_checkpoint().op_id());
  } else if (req->has_from_cdc_sdk_checkpoint()) {
    *cdc_sdk_op_id = req->from_cdc_sdk_checkpoint();
    *op_id = OpId::FromPB(*cdc_sdk_op_id);
  } else {
    return false;
  }
  return true;
}

// Check for compatibility whether CDC can be setup on the table
// This essentially checks that the table should not be a REDIS table since we do not support it
// and if it's a YSQL or YCQL one, it should have a primary key
Status CheckCdcCompatibility(const std::shared_ptr<client::YBTable>& table) {
  // return if it is a CQL table because they always have a user specified primary key
  if (table->table_type() == client::YBTableType::YQL_TABLE_TYPE) {
    LOG(INFO) << "Returning while checking CDC compatibility, table is a YCQL table";
    return Status::OK();
  }

  if (table->table_type() == client::YBTableType::REDIS_TABLE_TYPE) {
    return STATUS(InvalidArgument, "Cannot setup CDC on YEDIS_TABLE");
  }

  // Check if YSQL table has a primary key. CQL tables always have a
  // user specified primary key.
  if (!YsqlTableHasPrimaryKey(table->schema())) {
    return STATUS(InvalidArgument, "Cannot setup CDC on table without primary key");
  }

  return Status::OK();
}

CoarseTimePoint GetDeadline(const RpcContext& context, client::YBClient* client) {
  CoarseTimePoint deadline = context.GetClientDeadline();
  if (deadline == CoarseTimePoint::max()) {  // Not specified by user.
    deadline = CoarseMonoClock::now() + client->default_rpc_timeout();
  }
  return deadline;
}

Status VerifyArg(const SetCDCCheckpointRequestPB& req) {
  if (!req.has_checkpoint() && !req.has_bootstrap()) {
    return STATUS(InvalidArgument, "OpId is required to set checkpoint");
  }

  if (!req.has_tablet_id()) {
    return STATUS(InvalidArgument, "Tablet ID is required to set checkpoint");
  }

  if (!req.has_stream_id()) {
    return STATUS(InvalidArgument, "Stream ID is required to set checkpoint");
  }

  if (req.has_checkpoint()) {
    OpId op_id = OpId::FromPB(req.checkpoint().op_id());
    if (op_id.term < OpId::Invalid().term || op_id.index < OpId::Invalid().index) {
      LOG(WARNING) << "Received Invalid OpId " << op_id;
      return STATUS_FORMAT(InvalidArgument, "Valid OpId is required to set checkpoint : $0", op_id);
    }
  }
  return Status::OK();
}

}  // namespace

template <class ReqType, class RespType>
bool CDCServiceImpl::CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc) {
  TRACE("Received RPC $0: $1", rpc->ToString(), req->DebugString());
  if (PREDICT_FALSE(!context_)) {
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS(ServiceUnavailable, "Tablet Server is not running"),
        CDCErrorPB::NOT_RUNNING,
        rpc);
    return false;
  }
  return true;
}

void CDCServiceImpl::InitNewTabletStreamEntry(
    const xrepl::StreamId& stream_id,
    const TabletId& tablet_id,
    std::vector<ProducerTabletInfo>* producer_entries_modified,
    std::vector<CDCStateTableEntry>* entries_to_insert) {
  // For CDCSDK the initial checkpoint for each tablet will be maintained
  // in cdc_state table as -1.-1(Invalid), which is the default value of 'op_id'. Checkpoint will be
  // updated when client call setCDCCheckpoint.
  const auto op_id = OpId::Invalid();
  CDCStateTableEntry entry(tablet_id, stream_id);
  entry.checkpoint = op_id;
  entry.active_time = 0;
  entry.cdc_sdk_safe_time = 0;
  entries_to_insert->push_back(std::move(entry));

  producer_entries_modified->push_back(
      {.replication_group_id = {}, .stream_id = stream_id, .tablet_id = tablet_id});

  impl_->AddTabletCheckpoint(op_id, stream_id, tablet_id);
}

Result<NamespaceId> CDCServiceImpl::GetNamespaceId(
    const std::string& ns_name, YQLDatabase db_type) {
  master::GetNamespaceInfoResponsePB namespace_info_resp;
  RETURN_NOT_OK(
      client()->GetNamespaceInfo(std::string(), ns_name, db_type, &namespace_info_resp));

  return namespace_info_resp.namespace_().id();
}

Result<EnumOidLabelMap> CDCServiceImpl::GetEnumMapFromCache(
    const NamespaceName& ns_name, bool cql_namespace) {
  {
    yb::SharedLock<decltype(mutex_)> l(mutex_);
    if (enumlabel_cache_.find(ns_name) != enumlabel_cache_.end()) {
      return enumlabel_cache_.at(ns_name);
    }
  }
  return UpdateEnumCacheAndGetMap(ns_name, cql_namespace);
}

Result<EnumOidLabelMap> CDCServiceImpl::UpdateEnumCacheAndGetMap(
    const NamespaceName& ns_name, bool cql_namespace) {
  std::lock_guard l(mutex_);
  if (enumlabel_cache_.find(ns_name) == enumlabel_cache_.end()) {
    if (cql_namespace) {
      EnumOidLabelMap empty_map;
      enumlabel_cache_[ns_name] = empty_map;
    } else {
      return UpdateEnumMapInCacheUnlocked(ns_name);
    }
  }
  return enumlabel_cache_.at(ns_name);
}

Result<EnumOidLabelMap> CDCServiceImpl::UpdateEnumMapInCacheUnlocked(const NamespaceName& ns_name) {
  EnumOidLabelMap enum_oid_label_map = VERIFY_RESULT(client()->GetPgEnumOidLabelMap(ns_name));
  enumlabel_cache_[ns_name] = enum_oid_label_map;
  return enumlabel_cache_[ns_name];
}

Result<CompositeAttsMap> CDCServiceImpl::GetCompositeAttsMapFromCache(
    const NamespaceName& ns_name, bool cql_namespace) {
  {
    yb::SharedLock<decltype(mutex_)> l(mutex_);
    if (composite_type_cache_.find(ns_name) != composite_type_cache_.end()) {
      return composite_type_cache_.at(ns_name);
    }
  }
  return UpdateCompositeCacheAndGetMap(ns_name, cql_namespace);
}

Result<CompositeAttsMap> CDCServiceImpl::UpdateCompositeCacheAndGetMap(
    const NamespaceName& ns_name, bool cql_namespace) {
  std::lock_guard l(mutex_);
  if (composite_type_cache_.find(ns_name) == composite_type_cache_.end()) {
    if (cql_namespace) {
      CompositeAttsMap empty_map;
      composite_type_cache_[ns_name] = empty_map;
    } else {
      return UpdateCompositeMapInCacheUnlocked(ns_name);
    }
  }
  return composite_type_cache_.at(ns_name);
}

Result<CompositeAttsMap> CDCServiceImpl::UpdateCompositeMapInCacheUnlocked(
    const NamespaceName& ns_name) {
  CompositeAttsMap enum_oid_label_map = VERIFY_RESULT(client()->GetPgCompositeAttsMap(ns_name));
  composite_type_cache_[ns_name] = enum_oid_label_map;
  return composite_type_cache_[ns_name];
}

Status CDCServiceImpl::CreateCDCStreamForNamespace(
    const CreateCDCStreamRequestPB* req,
    CreateCDCStreamResponsePB* resp,
    CoarseTimePoint deadline) {
  // Used to delete streams in case of failure.
  CDCCreationState creation_state;

  auto scope_exit = ScopeExit([this, &creation_state] { RollbackPartialCreate(creation_state); });

  auto ns_id = VERIFY_RESULT_OR_SET_CODE(
      GetNamespaceId(req->namespace_name(), req->db_type()), CDCError(CDCErrorPB::INVALID_REQUEST));

  // Generate a stream id by calling CreateCDCStream, and also setup the stream in the master.
  std::unordered_map<std::string, std::string> options = GetCreateCDCStreamOptions(req);

  // Forward request to master directly since we support creating CDCSDK stream for a namespace
  // atomically in master now.
  // If FLAGS_TEST_ysql_yb_enable_replication_commands, populate the namespace id in the newly added
  // namespace_id field, otherwise use the table_id as done before.
  bool populate_namespace_id_as_table_id = !FLAGS_TEST_ysql_yb_enable_replication_commands;
  xrepl::StreamId db_stream_id = VERIFY_RESULT_OR_SET_CODE(
      client()->CreateCDCSDKStreamForNamespace(ns_id, options, populate_namespace_id_as_table_id),
      CDCError(CDCErrorPB::INTERNAL_ERROR));
  resp->set_db_stream_id(db_stream_id.ToString());
  return Status::OK();
}

void CDCServiceImpl::CreateCDCStream(
    const CreateCDCStreamRequestPB* req, CreateCDCStreamResponsePB* resp, RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(
      req->has_table_id() || req->has_namespace_name(),
      STATUS(InvalidArgument, "Table ID or Database name is required to create CDC stream"),
      resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST,
      context);

  bool is_xcluster = req->source_type() == XCLUSTER;
  if (is_xcluster || req->has_table_id()) {
    std::shared_ptr<client::YBTable> table;
    Status s = client()->OpenTable(req->table_id(), &table);
    RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::TABLE_NOT_FOUND, context);

    // We don't allow CDC on YEDIS and tables without a primary key.
    if (req->record_format() != CDCRecordFormat::WAL) {
      s = CheckCdcCompatibility(table);
      RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);
    }

    std::unordered_map<std::string, std::string> options = GetCreateCDCStreamOptions(req);

    auto stream_id = RPC_VERIFY_RESULT(
        client()->CreateCDCStream(req->table_id(), options), resp->mutable_error(),
        CDCErrorPB::INTERNAL_ERROR, context);

    resp->set_stream_id(stream_id.ToString());

    // Add stream to cache.
    AddStreamMetadataToCache(
        stream_id,
        std::make_shared<StreamMetadata>(
            "",
            std::vector<TableId>{req->table_id()},
            req->record_type(),
            req->record_format(),
            req->source_type(),
            req->checkpoint_type(),
            StreamModeTransactional(req->transactional())));
  } else if (req->has_namespace_name()) {
    // Return error if we see that no checkpoint type has been populated.
    RPC_CHECK_AND_RETURN_ERROR(
        req->has_checkpoint_type(),
        STATUS(InvalidArgument, "Checkpoint type is required to create a CDCSDK stream"),
        resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

    auto deadline = GetDeadline(context, client());
    Status status = CreateCDCStreamForNamespace(req, resp, deadline);
    CDCError error(status);

    if (!status.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), status, error.value(), &context);
      return;
    }
  }

  context.RespondSuccess();
}

Result<SetCDCCheckpointResponsePB> CDCServiceImpl::SetCDCCheckpoint(
    const SetCDCCheckpointRequestPB& req, CoarseTimePoint deadline) {
  VLOG(1) << "Received SetCDCCheckpoint request " << req.ShortDebugString();

  RETURN_NOT_OK_SET_CODE(VerifyArg(req), CDCError(CDCErrorPB::INVALID_REQUEST));

  auto stream_id = VERIFY_STRING_TO_STREAM_ID(req.stream_id());
  auto record = VERIFY_RESULT(GetStream(stream_id));
  if (record->GetCheckpointType() != EXPLICIT) {
    LOG(WARNING) << "Setting the checkpoint explicitly even though the checkpoint type is implicit";
  }

  auto tablet_peer = context_->LookupTablet(req.tablet_id());

  // Case-1 The connected tserver does not contain the requested tablet_id.
  // Case-2 The connected tserver does not contain the tablet LEADER.
  if ((!tablet_peer || tablet_peer->IsNotLeader()) && req.serve_as_proxy()) {
    // Proxy to the leader
    auto ts_leader = GetLeaderTServer(req.tablet_id(), false /* use_cache */);
    RETURN_NOT_OK_SET_CODE(ts_leader, CDCError(CDCErrorPB::NOT_LEADER));
    auto cdc_proxy = GetCDCServiceProxy(*ts_leader);

    SetCDCCheckpointRequestPB new_req;
    new_req.CopyFrom(req);
    new_req.set_serve_as_proxy(false);

    rpc::RpcController rpc;
    rpc.set_deadline(deadline);
    SetCDCCheckpointResponsePB resp;
    if (tablet_peer) {
      VLOG(2) << "Current tablet_peer: " << tablet_peer->permanent_uuid()
              << "is not a LEADER for tablet_id: " << req.tablet_id()
              << " so handovering to the actual LEADER.";
    } else {
      VLOG(2) << "Current TServer doesn not host tablet_id: " << req.tablet_id()
              << " so handovering to the actual LEADER.";
    }

    RETURN_NOT_OK_SET_CODE(
        cdc_proxy->SetCDCCheckpoint(new_req, &resp, &rpc), CDCError(CDCErrorPB::INTERNAL_ERROR));
    return resp;
  }

  // Case-3 The connected tserver is the tablet LEADER but not yet ready.
  if (!tablet_peer || !tablet_peer->IsLeaderAndReady()) {
    VLOG(2) << "Current LEADER is not ready to serve tablet_id: " << req.tablet_id();
    return STATUS(
        LeaderNotReadyToServe, "Not ready to serve", CDCError(CDCErrorPB::LEADER_NOT_READY));
  }

  RETURN_NOT_OK_SET_CODE(
      CheckCanServeTabletData(*tablet_peer->tablet_metadata()),
      CDCError(CDCErrorPB::LEADER_NOT_READY));

  ProducerTabletInfo producer_tablet{
      {}, VERIFY_STRING_TO_STREAM_ID(req.stream_id()), req.tablet_id()};
  RETURN_NOT_OK_SET_CODE(
      CheckTabletValidForStream(producer_tablet), CDCError(CDCErrorPB::INVALID_REQUEST));

  OpId checkpoint;
  HybridTime cdc_sdk_safe_time = HybridTime::kInvalid;
  bool set_latest_entry = req.bootstrap();
  const string err_message = strings::Substitute(
      "Unable to get the latest entry op id from "
      "peer $0 and tablet $1 because its log object hasn't been initialized",
      tablet_peer->permanent_uuid(), tablet_peer->tablet_id());
  if (set_latest_entry) {
    // CDC will keep sending log init failure until FLAGS_TEST_cdc_log_init_failure_timeout_seconds
    // is expired.
    auto cdc_log_init_failure_timeout_seconds =
        GetAtomicFlag(&FLAGS_TEST_cdc_log_init_failure_timeout_seconds);
    if (cdc_log_init_failure_timeout_seconds > 0) {
      if (test_expire_time_cdc_log_init_failure == MonoTime::kUninitialized) {
        test_expire_time_cdc_log_init_failure =
            MonoTime::Now() + MonoDelta::FromSeconds(cdc_log_init_failure_timeout_seconds);
      }

      if (MonoTime::Now() < test_expire_time_cdc_log_init_failure) {
        return STATUS(ServiceUnavailable, err_message, CDCError(CDCErrorPB::LEADER_NOT_READY));
      }
    }

    if (!tablet_peer->log_available()) {
      return STATUS(ServiceUnavailable, err_message, CDCError(CDCErrorPB::LEADER_NOT_READY));
    }
    checkpoint = tablet_peer->log()->GetLatestEntryOpId();
  } else {
    checkpoint = OpId::FromPB(req.checkpoint().op_id());
  }

  if (!tablet_peer->log_available()) {
    return STATUS(ServiceUnavailable, err_message, CDCError(CDCErrorPB::LEADER_NOT_READY));
  }
  auto result = tablet_peer->LeaderSafeTime();
  if (!result.ok()) {
    LOG(WARNING) << "Could not find the leader safe time successfully";
  } else {
    cdc_sdk_safe_time = *result;
  }

  // If bootstrap is false and valid cdcsdk_safe_time is set, than set the input safe_time.
  if (!set_latest_entry && HybridTime::FromPB(req.cdc_sdk_safe_time()) != HybridTime::kInvalid) {
    cdc_sdk_safe_time = HybridTime::FromPB(req.cdc_sdk_safe_time());
  }

  // If set_latest_entry is true i.e. the case of bootstrap, we know it is not a snapshot call.
  // Similarly, if bootstrap (set_latest_entry) is false, we know it is a snapshot call.
  RETURN_NOT_OK_SET_CODE(
      UpdateCheckpointAndActiveTime(
          producer_tablet, checkpoint, checkpoint, GetCurrentTimeMicros(),
          CDCRequestSource::CDCSDK, true, cdc_sdk_safe_time, !set_latest_entry),
      CDCError(CDCErrorPB::INTERNAL_ERROR));

  if (req.has_initial_checkpoint() || set_latest_entry) {
    auto result = SetInitialCheckPoint(checkpoint, req.tablet_id(), tablet_peer, cdc_sdk_safe_time);
    RETURN_NOT_OK_SET_CODE(result, CDCError(CDCErrorPB::INTERNAL_ERROR));
  }
  return SetCDCCheckpointResponsePB();
}

void CDCServiceImpl::GetTabletListToPollForCDC(
    const GetTabletListToPollForCDCRequestPB* req,
    GetTabletListToPollForCDCResponsePB* resp,
    RpcContext context) {
  VLOG(1) << "Received GetTabletListToPollForCDC request " << req->ShortDebugString();

  RPC_CHECK_AND_RETURN_ERROR(
      !(req->has_table_info() && req->table_info().table_id().empty() &&
        req->table_info().stream_id().empty()),
      STATUS(InvalidArgument, "StreamId and tableId required"), resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST, context);

  const auto& table_id = req->table_info().table_id();
  const auto req_stream_id = RPC_VERIFY_STRING_TO_STREAM_ID(req->table_info().stream_id());

  // Look up stream in sys catalog.
  std::vector<TableId> table_ids;
  NamespaceId ns_id;
  std::unordered_map<std::string, std::string> options;
  StreamModeTransactional transactional(false);
  RPC_STATUS_RETURN_ERROR(
      client()->GetCDCStream(req_stream_id, &ns_id, &table_ids, &options, &transactional),
      resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  // This means the table has not been added to the stream's metadata.
  if (std::find(table_ids.begin(), table_ids.end(), table_id) == table_ids.end()) {
    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS(NotFound, Format("Table $0 not found under stream", req->table_info().table_id())),
        CDCErrorPB::TABLE_NOT_FOUND, &context);
    return;
  }

  client::YBTableName table_name;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  table_name.set_table_id(table_id);
  RPC_STATUS_RETURN_ERROR(
      client()->GetTablets(
          table_name, 0, &tablets, /* partition_list_version =*/nullptr,
          RequireTabletsRunning::kFalse, master::IncludeInactive::kTrue),
      resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  std::unordered_map<TabletId, master::TabletLocationsPB> tablet_id_to_tablet_locations_map;
  if (!req->has_tablet_id() || req->tablet_id().empty()) {
    std::set<TabletId> active_or_hidden_tablets;
    std::set<TabletId> parent_tablets;
    std::map<TabletId, TabletId> child_to_parent_mapping;

    for (const auto& tablet : tablets) {
      active_or_hidden_tablets.insert(tablet.tablet_id());
      if (tablet.has_split_parent_tablet_id() && !tablet.split_parent_tablet_id().empty()) {
        const auto& parent_tablet_id = tablet.split_parent_tablet_id();
        parent_tablets.insert(parent_tablet_id);
        child_to_parent_mapping.insert({tablet.tablet_id(), parent_tablet_id});
      }
      tablet_id_to_tablet_locations_map[tablet.tablet_id()] = tablet;
    }

    std::vector<std::pair<TabletId, CDCSDKCheckpointPB>> tablet_checkpoint_pairs;
    RPC_STATUS_RETURN_ERROR(
        GetTabletIdsToPoll(
            req_stream_id, active_or_hidden_tablets, parent_tablets, child_to_parent_mapping,
            &tablet_checkpoint_pairs),
        resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

    resp->mutable_tablet_checkpoint_pairs()->Reserve(
        static_cast<int>(tablet_checkpoint_pairs.size()));
    for (auto tablet_checkpoint_pair : tablet_checkpoint_pairs) {
      auto tablet_checkpoint_pair_pb = resp->add_tablet_checkpoint_pairs();

      tablet_checkpoint_pair_pb->mutable_tablet_locations()->CopyFrom(
          tablet_id_to_tablet_locations_map[tablet_checkpoint_pair.first]);
      tablet_checkpoint_pair_pb->mutable_cdc_sdk_checkpoint()->CopyFrom(
          tablet_checkpoint_pair.second);
    }
  } else {
    // If the request had tablet_id populated, we will only return the details of the child tablets
    // of the specified tablet.
    boost::container::small_vector<TabletId, 2> child_tablet_ids;
    for (const auto& cur_tablet : tablets) {
      if (cur_tablet.has_split_parent_tablet_id() &&
          cur_tablet.split_parent_tablet_id() == req->tablet_id()) {
        child_tablet_ids.push_back(cur_tablet.tablet_id());
        tablet_id_to_tablet_locations_map[cur_tablet.tablet_id()] = cur_tablet;
      } else if (cur_tablet.tablet_id() == req->tablet_id()) {
        tablet_id_to_tablet_locations_map[cur_tablet.tablet_id()] = cur_tablet;
      }
    }

    // Get the checkpoint from the parent tablet.
    CDCSDKCheckpointPB parent_checkpoint_pb;
    {
      auto last_checkpoint = RPC_VERIFY_RESULT(
          GetLastCheckpointFromCdcState(req_stream_id, req->tablet_id(), CDCRequestSource::CDCSDK),
          resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
      parent_checkpoint_pb.CopyFrom(last_checkpoint);
    }

    for (const auto& child_tablet_id : child_tablet_ids) {
      ProducerTabletInfo cur_child_tablet = {{}, req_stream_id, child_tablet_id};

      auto tablet_checkpoint_pair_pb = resp->add_tablet_checkpoint_pairs();
      tablet_checkpoint_pair_pb->mutable_tablet_locations()->CopyFrom(
          tablet_id_to_tablet_locations_map[child_tablet_id]);

      auto last_checkpoint = RPC_VERIFY_RESULT(
          GetLastCheckpointFromCdcState(
              cur_child_tablet.stream_id, cur_child_tablet.tablet_id, CDCRequestSource::CDCSDK),
          resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
      // If the child's checkpoint is invalid, we will rely on the parent tablet's checkpoint.
      if (last_checkpoint.term() > 0 && last_checkpoint.index() > 0) {
        CDCSDKCheckpointPB checkpoint_pb;
        checkpoint_pb.CopyFrom(last_checkpoint);
        tablet_checkpoint_pair_pb->mutable_cdc_sdk_checkpoint()->CopyFrom(checkpoint_pb);
      } else {
        tablet_checkpoint_pair_pb->mutable_cdc_sdk_checkpoint()->CopyFrom(parent_checkpoint_pb);
      }
    }
  }

  context.RespondSuccess();
}

void CDCServiceImpl::DeleteCDCStream(
    const DeleteCDCStreamRequestPB* req, DeleteCDCStreamResponsePB* resp, RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  LOG(INFO) << "Received DeleteCDCStream request " << req->ShortDebugString();

  RPC_CHECK_AND_RETURN_ERROR(
      !req->stream_id().empty(),
      STATUS(InvalidArgument, "Stream ID or Database stream ID is required to delete CDC stream"),
      resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST,
      context);

  vector<xrepl::StreamId> streams;
  for (const auto& stream_id : req->stream_id()) {
    streams.emplace_back(RPC_VERIFY_STRING_TO_STREAM_ID(stream_id));
  }

  Status s = client()->DeleteCDCStream(
      streams,
      (req->has_force_delete() && req->force_delete()),
      (req->has_ignore_errors() && req->ignore_errors()));
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  context.RespondSuccess();
}

void CDCServiceImpl::ListTablets(
    const ListTabletsRequestPB* req, ListTabletsResponsePB* resp, RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(
      req->has_stream_id(),
      STATUS(InvalidArgument, "Stream ID is required to list tablets"),
      resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST,
      context);

  auto stream_id = RPC_VERIFY_STRING_TO_STREAM_ID(req->stream_id());
  auto tablets = RPC_VERIFY_RESULT(
      GetTablets(stream_id), resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  if (!req->local_only()) {
    resp->mutable_tablets()->Reserve(tablets.size());
  }

  for (const auto& tablet : tablets) {
    // Filter local tablets if needed.
    if (req->local_only()) {
      bool is_local = false;
      for (const auto& replica : tablet.replicas()) {
        if (replica.ts_info().permanent_uuid() == context_->permanent_uuid()) {
          is_local = true;
          break;
        }
      }

      if (!is_local) {
        continue;
      }
    }

    auto res = resp->add_tablets();
    res->set_tablet_id(tablet.tablet_id());
    res->mutable_tservers()->Reserve(tablet.replicas_size());
    for (const auto& replica : tablet.replicas()) {
      auto tserver = res->add_tservers();
      tserver->mutable_broadcast_addresses()->CopyFrom(replica.ts_info().broadcast_addresses());
      if (tserver->broadcast_addresses_size() == 0) {
        LOG(WARNING) << "No public broadcast addresses found for "
                     << replica.ts_info().permanent_uuid() << ".  Using private addresses instead.";
        tserver->mutable_broadcast_addresses()->CopyFrom(replica.ts_info().private_rpc_addresses());
      }
    }
  }

  context.RespondSuccess();
}

Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> CDCServiceImpl::GetTablets(
    const xrepl::StreamId& stream_id,
    bool ignore_errors) {
  auto stream_metadata = VERIFY_RESULT(GetStream(stream_id, RefreshStreamMapOption::kAlways));
  client::YBTableName table_name;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> all_tablets;

  std::vector<TableId> table_ids = stream_metadata->GetTableIds();

  for (const auto& table_id : table_ids) {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    table_name.set_table_id(table_id);
    Status s = client()->GetTablets(
        table_name, 0, &tablets, /* partition_list_version =*/nullptr,
        RequireTabletsRunning::kFalse, master::IncludeInactive::kTrue);

    if (!s.ok()) {
      if (ignore_errors) {
        LOG(WARNING) << "Fetching tablets for table " << table_name.table_id()
                     << " failed with error: " << s;
        continue;
      }

      return s;
    }

    all_tablets.MergeFrom(tablets);
  }

  return all_tablets;
}

Result<TabletCheckpoint> CDCServiceImpl::TEST_GetTabletInfoFromCache(
    const ProducerTabletInfo& producer_tablet) {
  return impl_->TEST_GetTabletInfoFromCache(producer_tablet);
}

bool CDCServiceImpl::IsReplicationPausedForStream(const std::string& stream_id) const {
  SharedLock l(mutex_);
  return paused_xcluster_producer_streams_.contains(stream_id);
}

void CDCServiceImpl::GetChanges(
    const GetChangesRequestPB* req, GetChangesResponsePB* resp, RpcContext context) {
  const auto start_time = MonoTime::Now();
  RPC_CHECK_AND_RETURN_ERROR(
      get_changes_rpc_sem_.TryAcquire(), STATUS(LeaderNotReadyToServe, "Not ready to serve"),
      resp->mutable_error(), CDCErrorPB::LEADER_NOT_READY, context);

  auto scope_exit = ScopeExit([this] { get_changes_rpc_sem_.Release(); });

  if (!CheckOnline(req, resp, &context)) {
    return;
  }
  YB_LOG_EVERY_N_SECS(INFO, 300) << "Received GetChanges request " << req->ShortDebugString();

  RPC_CHECK_AND_RETURN_ERROR(
      req->has_tablet_id(),
      STATUS(InvalidArgument, "Tablet ID is required to get CDC changes"),
      resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST,
      context);
  RPC_CHECK_AND_RETURN_ERROR(
      req->has_stream_id() || req->has_db_stream_id(),
      STATUS(InvalidArgument, "Stream ID/DB Stream ID is required to get CDC changes"),
      resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST,
      context);

  auto stream_id = RPC_VERIFY_STRING_TO_STREAM_ID(
      req->has_db_stream_id() ? req->db_stream_id() : req->stream_id());

  CoarseTimePoint deadline = GetDeadline(context, client());

  // Check that requested tablet_id is part of the CDC stream.
  ProducerTabletInfo producer_tablet = {{}, stream_id, req->tablet_id()};

  auto status = CheckTabletValidForStream(producer_tablet);
  if (!status.ok()) {
    RPC_STATUS_RETURN_ERROR(
        CheckTabletValidForStream(producer_tablet), resp->mutable_error(),
        status.IsTabletSplit() ? CDCErrorPB::TABLET_SPLIT : CDCErrorPB::INVALID_REQUEST, context);
  }

  auto tablet_peer = context_->LookupTablet(req->tablet_id());

  auto original_leader_term = tablet_peer ? tablet_peer->LeaderTerm() : OpId::kUnknownTerm;

  if ((!tablet_peer || tablet_peer->IsNotLeader()) && req->serve_as_proxy()) {
    // Forward GetChanges() to tablet leader. This commonly happens in Kubernetes setups.
    auto context_ptr = std::make_shared<RpcContext>(std::move(context));
    TabletLeaderGetChanges(req, resp, context_ptr, tablet_peer);
    return;
  }

  // If we can't serve this tablet...
  RPC_CHECK_NE_AND_RETURN_ERROR(
      tablet_peer, nullptr, STATUS_FORMAT(NotFound, "Tablet $0 not found", req->tablet_id()),
      resp->mutable_error(), CDCErrorPB::TABLET_NOT_FOUND, context);

  RPC_CHECK_AND_RETURN_ERROR(
      !tablet_peer->IsNotLeader(),
      STATUS_FORMAT(
          NotFound, "Not leader for $0 $1", req->tablet_id(), tablet_peer->LeaderStatus()),
      resp->mutable_error(),
      CDCErrorPB::TABLET_NOT_FOUND,
      context);

  RPC_CHECK_AND_RETURN_ERROR(
      tablet_peer->IsLeaderAndReady(),
      STATUS(LeaderNotReadyToServe, "Not ready to serve"),
      resp->mutable_error(),
      CDCErrorPB::LEADER_NOT_READY,
      context);

  auto stream_meta_ptr = RPC_VERIFY_RESULT(
      GetStream(stream_id, RefreshStreamMapOption::kIfInitiatedState), resp->mutable_error(),
      CDCErrorPB::INTERNAL_ERROR, context);
  StreamMetadata& record = *stream_meta_ptr;

  if (record.GetSourceType() == CDCSDK) {
    RPC_STATUS_RETURN_ERROR(
        CheckStreamActive(producer_tablet), resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR,
        context);
    impl_->UpdateActiveTime(producer_tablet);

    if (IsCDCSDKSnapshotDone(*req)) {
      // Remove 'kCDCSDKSnapshotKey' from the colocated snapshot row, to indicate that the snapshot
      // is done.
      RPC_STATUS_RETURN_ERROR(
          UpdateSnapshotDone(
              stream_id, req->tablet_id(),
              tablet_peer->tablet_metadata()->colocated() ? req->table_id() : "",
              req->from_cdc_sdk_checkpoint()),
          resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

      // We will return the streaming checkpoint as a response, so that the connector will start
      // streaming from that point.
      auto streaming_checkpoint_pb = RPC_VERIFY_RESULT(
          GetLastCheckpointFromCdcState(stream_id, req->tablet_id(), CDCRequestSource::CDCSDK),
          resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
      streaming_checkpoint_pb.set_key("");
      streaming_checkpoint_pb.set_write_id(0);
      resp->mutable_cdc_sdk_checkpoint()->CopyFrom(streaming_checkpoint_pb);

      context.RespondSuccess();
      return;
    }
  }
  // This is the leader tablet, so mark cdc as enabled.
  SetCDCServiceEnabled();

  OpId from_op_id;
  CDCSDKCheckpointPB cdc_sdk_from_op_id;

  OpId explicit_op_id;
  CDCSDKCheckpointPB cdc_sdk_explicit_op_id;
  uint64_t cdc_sdk_explicit_safe_time = 0;

  bool got_explicit_checkpoint_from_request = false;
  if (record.GetCheckpointType() == EXPLICIT) {
    got_explicit_checkpoint_from_request = GetExplicitOpIdAndSafeTime(
        req, &explicit_op_id, &cdc_sdk_explicit_op_id, &cdc_sdk_explicit_safe_time);
  }

  // Get opId from request.
  if (!GetFromOpId(req, &from_op_id, &cdc_sdk_from_op_id)) {
    auto last_checkpoint = RPC_VERIFY_RESULT(
        GetLastCheckpoint(producer_tablet, stream_meta_ptr.get()->GetSourceType(), false),
        resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
    LOG(WARNING) << "GetChanges called on T " << req->tablet_id() << " S " << req->stream_id()
                 << " without an index. Using last checkpoint: " << last_checkpoint;
    if (record.GetSourceType() == XCLUSTER) {
      from_op_id = last_checkpoint;
    } else {
      // This is the initial checkpoint set in cdc_state table, during create of CDCSDK
      // create stream, so throw an exeception to client to call setCDCCheckpoint or  take Snapshot.
      if (last_checkpoint == OpId::Invalid()) {
        SetupErrorAndRespond(
            resp->mutable_error(),
            STATUS_FORMAT(
                InvalidArgument,
                "Invalid checkpoint $0 for tablet $1. Hint: set checkpoint explicitly or take "
                "snapshot",
                last_checkpoint, req->tablet_id()),
            CDCErrorPB::INTERNAL_ERROR, &context);
        return;
      }
      last_checkpoint.ToPB(&cdc_sdk_from_op_id);
      from_op_id = OpId::FromPB(cdc_sdk_from_op_id);
    }
  }

  bool is_replication_paused_for_stream = IsReplicationPausedForStream(req->stream_id());
  if (is_replication_paused_for_stream || PREDICT_FALSE(FLAGS_TEST_block_get_changes)) {
    if (is_replication_paused_for_stream && VLOG_IS_ON(1)) {
      YB_LOG_EVERY_N_SECS(INFO, 300)
          << "Replication is paused from the producer for stream: " << req->stream_id();
    }
    // Returning success to slow down polling on the consumer side while replication is paused or
    // early exit for testing purpose.
    from_op_id.ToPB(resp->mutable_checkpoint()->mutable_op_id());
    context.RespondSuccess();
    return;
  }

  int64_t last_readable_index;
  consensus::ReplicateMsgsHolder msgs_holder;
  MemTrackerPtr mem_tracker = impl_->GetMemTracker(tablet_peer, producer_tablet);

  // Calculate deadline to be passed to GetChanges.
  CoarseTimePoint get_changes_deadline = CoarseTimePoint::max();
  if (deadline != CoarseTimePoint::max()) {
    // Check if we are too close to calculate a safe deadline.
    RPC_CHECK_AND_RETURN_ERROR(
        deadline - CoarseMonoClock::Now() > 1ms,
        STATUS(TimedOut, "Too close to rpc timeout to call GetChanges."), resp->mutable_error(),
        CDCErrorPB::INTERNAL_ERROR, context);

    // Calculate a safe deadline so that CdcProducer::GetChanges times out
    // 20% faster than CdcServiceImpl::GetChanges. This gives enough
    // time (unless timeouts are unrealistically small) for CdcServiceImpl::GetChanges
    // to finish post-processing and return the partial results without itself timing out.
    const auto safe_deadline =
        deadline - (FLAGS_cdc_read_rpc_timeout_ms * 1ms * FLAGS_cdc_read_safe_deadline_ratio);
    get_changes_deadline = ToCoarse(MonoTime::FromUint64(safe_deadline.time_since_epoch().count()));
  }

  bool report_tablet_split = false;
  // Read the latest changes from the Log.
  if (record.GetSourceType() == XCLUSTER) {
    // Check AutoFlags version and fail early on errors before scanning the WAL.
    if (!ValidateAutoFlagsConfigVersion(*req, *resp, context)) {
      return;
    }
    TEST_SYNC_POINT("GetChanges::AfterFirstValidateAutoFlagsConfigVersion1");
    TEST_SYNC_POINT("GetChanges::AfterFirstValidateAutoFlagsConfigVersion2");
    status = GetChangesForXCluster(
        stream_id, req->tablet_id(), from_op_id, tablet_peer,
        std::bind(
            &CDCServiceImpl::UpdateChildrenTabletsOnSplitOpForXCluster, this, producer_tablet, _1),
        mem_tracker, get_changes_deadline, &record, &msgs_holder, resp, &last_readable_index);

    // Check AutoFlags version again if we are sending any records back.
    if (resp->records_size() > 0 && !ValidateAutoFlagsConfigVersion(*req, *resp, context)) {
      return;
    }
  } else {
    uint64_t commit_timestamp;
    OpId last_streamed_op_id;
    auto cached_schema_details = impl_->GetOrAddSchema(producer_tablet, req->need_schema_info());

    auto tablet_ptr = RPC_VERIFY_RESULT(
        tablet_peer->shared_tablet_safe(), resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR,
        context);

    auto namespace_name = tablet_ptr->metadata()->namespace_name();
    auto last_sent_checkpoint = impl_->GetLastStreamedOpId(producer_tablet);
    // If from_op_id is more than the last sent op_id, it indicates a potential stale schema entry.
    if (last_sent_checkpoint == boost::none ||
        OpId::FromPB(cdc_sdk_from_op_id) != *last_sent_checkpoint) {
      VLOG(1) << "Stale entry in the cache, because last sent checkpoint: " << *last_sent_checkpoint
              << " less than from_op_id: " << OpId::FromPB(cdc_sdk_from_op_id)
              << ", get proper schema version from system catalog.";
      cached_schema_details.clear();
    }
    bool cql_namespace = tablet_peer->tablet()->table_type() == YQL_TABLE_TYPE;
    auto enum_map = RPC_VERIFY_RESULT(
        GetEnumMapFromCache(namespace_name, cql_namespace), resp->mutable_error(),
        CDCErrorPB::INTERNAL_ERROR, context);

    auto composite_atts_map = RPC_VERIFY_RESULT(
        GetCompositeAttsMapFromCache(namespace_name, cql_namespace), resp->mutable_error(),
        CDCErrorPB::INTERNAL_ERROR, context);

    status = GetChangesForCDCSDK(
        stream_id, req->tablet_id(), cdc_sdk_from_op_id, record, tablet_peer, mem_tracker, enum_map,
        composite_atts_map, client(), &msgs_holder, resp, &commit_timestamp, &cached_schema_details,
        &last_streamed_op_id, req->safe_hybrid_time(), req->wal_segment_index(),
        &last_readable_index, tablet_peer->tablet_metadata()->colocated() ? req->table_id() : "",
        get_changes_deadline);
    // This specific error from the docdb_pgapi layer is used to identify enum cache entry is
    // out of date, hence we need to repopulate.
    if (status.IsCacheMissError()) {
      {
        string message = status.ToUserMessage(false);
        if (message == "enum") {
          // Recreate the enum cache entry for the corresponding namespace.
          std::lock_guard l(mutex_);
          enum_map = RPC_VERIFY_RESULT(
              UpdateEnumMapInCacheUnlocked(namespace_name), resp->mutable_error(),
              CDCErrorPB::INTERNAL_ERROR, context);
        } else if (message == "composite") {
          std::lock_guard l(mutex_);
          composite_atts_map = RPC_VERIFY_RESULT(
              UpdateCompositeMapInCacheUnlocked(namespace_name), resp->mutable_error(),
              CDCErrorPB::INTERNAL_ERROR, context);
        }
      }
      // Clean all the records which got added in the resp, till the enum cache miss failure is
      // encountered.
      resp->clear_cdc_sdk_proto_records();
      status = GetChangesForCDCSDK(
          stream_id, req->tablet_id(), cdc_sdk_from_op_id, record, tablet_peer, mem_tracker,
          enum_map, composite_atts_map, client(), &msgs_holder, resp, &commit_timestamp,
          &cached_schema_details, &last_streamed_op_id, req->safe_hybrid_time(),
          req->wal_segment_index(), &last_readable_index,
          tablet_peer->tablet_metadata()->colocated() ? req->table_id() : "", get_changes_deadline);
    }
    // This specific error indicates that a tablet split occured on the tablet.
    if (status.IsTabletSplit()) {
      LOG(INFO) << "Updating children tablets on detected split on tablet "
                << producer_tablet.tablet_id;
      status = UpdateChildrenTabletsOnSplitOpForCDCSDK(producer_tablet);
      RPC_STATUS_RETURN_ERROR(status, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

      report_tablet_split = true;
    }

    impl_->UpdateCDCStateMetadata(
        producer_tablet, commit_timestamp, cached_schema_details,
        OpId::FromPB(resp->cdc_sdk_checkpoint()));
  }

  if (FLAGS_enable_xcluster_stat_collection) {
    const auto latest_wal_index = tablet_peer->log()->GetLatestEntryOpId().index;
    auto sent_index = resp->checkpoint().op_id().index();
    auto num_records = resp->records_size();
    auto bytes_sent = num_records > 0 ? resp->ByteSizeLong() : 0;
    record.GetTabletMetadata(req->tablet_id())
        ->UpdateStats(start_time, status, num_records, bytes_sent, sent_index, latest_wal_index);
  }

  if (record.GetSourceType() == XCLUSTER) {
    auto tablet_metric_row =
        GetCDCTabletMetrics(producer_tablet, tablet_peer, record.GetSourceType());
    if (tablet_metric_row) {
      auto tablet_metric = std::static_pointer_cast<CDCTabletMetrics>(tablet_metric_row);
      tablet_metric->is_bootstrap_required->set_value(status.IsNotFound());
    }
  }

  VLOG(1) << "T " << req->tablet_id() << " sending GetChanges response " << AsString(*resp);
  RPC_STATUS_RETURN_ERROR(
      status,
      resp->mutable_error(),
      status.IsNotFound() ? CDCErrorPB::CHECKPOINT_TOO_OLD : CDCErrorPB::UNKNOWN_ERROR,
      context);
  tablet_peer = context_->LookupTablet(req->tablet_id());

  // Verify leadership was maintained for the duration of the GetChanges() read.
  RPC_CHECK_AND_RETURN_ERROR(
      tablet_peer && tablet_peer->IsLeaderAndReady() &&
          tablet_peer->LeaderTerm() == original_leader_term,
      STATUS_FORMAT(NotFound, "Not leader for $0", req->tablet_id()), resp->mutable_error(),
      CDCErrorPB::TABLET_NOT_FOUND, context);

  // Store information about the last server read & remote client ACK.
  uint64_t last_record_hybrid_time =
      resp->records_size() > 0
          ? resp->records(resp->records_size() - 1).time()
          : ((resp->cdc_sdk_proto_records_size() > 0 &&
              resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1)
                  .row_message()
                  .has_commit_time())
                 ? resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1)
                       .row_message()
                       .commit_time()
                 : 0);

  bool snapshot_bootstrap = IsCDCSDKSnapshotBootstrapRequest(cdc_sdk_from_op_id);
  if (record.GetCheckpointType() == IMPLICIT ||
      (record.GetCheckpointType() == EXPLICIT &&
       ((got_explicit_checkpoint_from_request && explicit_op_id != OpId::Invalid()) ||
        snapshot_bootstrap))) {
    bool is_snapshot = false;
    bool is_colocated = tablet_peer->tablet_metadata()->colocated();
    OpId snapshot_op_id = OpId::Invalid();
    std::string snapshot_key = "";

    // If snapshot operation or before image is enabled, don't allow compaction.
    HybridTime cdc_sdk_safe_time = HybridTime::kInvalid;
    if (record.GetCheckpointType() == EXPLICIT && cdc_sdk_explicit_safe_time != 0) {
      cdc_sdk_safe_time = HybridTime::FromPB(cdc_sdk_explicit_safe_time);
    } else if (req->safe_hybrid_time() != -1) {
      cdc_sdk_safe_time = HybridTime::FromPB(req->safe_hybrid_time());
    } else {
      YB_LOG_EVERY_N(WARNING, 10000)
          << "safe_hybrid_time is not present in request, using response to get safe_hybrid_time";
      cdc_sdk_safe_time = HybridTime::FromPB(resp->safe_hybrid_time());
    }

    if (UpdateCheckpointRequired(record, cdc_sdk_from_op_id, &is_snapshot)) {
      // This is the snapshot bootstrap operation, so taking the checkpoint from the resp.
      if (is_snapshot) {
        snapshot_op_id =
            OpId(resp->cdc_sdk_checkpoint().term(), resp->cdc_sdk_checkpoint().index());
        snapshot_key = (record.GetCheckpointType() == EXPLICIT) ?
                          req->explicit_cdc_sdk_checkpoint().key()
                            : req->from_cdc_sdk_checkpoint().key();

        if (snapshot_bootstrap) {
          LOG(INFO) << "Snapshot bootstrapping is initiated for tablet_id: " << req->tablet_id()
                    << " with stream_id: " << stream_id
                    << ", we will update the checkpoint: " << snapshot_op_id
                    << ", cdcsdk safe time: " << cdc_sdk_safe_time;
        }

        // If this is the first 'GetChanges'call with snapshot_key empty and the table_id set in the
        // request, this means this is the first snapshot call for a colocated tablet with the
        // requested table_id.
        if (snapshot_key.empty() && req->has_table_id() && record.GetSourceType() == CDCSDK &&
            is_colocated) {
          RPC_STATUS_RETURN_ERROR(
              InsertRowForColocatedTableInCDCStateTable(
                  producer_tablet, req->table_id(), snapshot_op_id, cdc_sdk_safe_time),
              resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
          LOG(INFO) << "Added row in cdc_state table for stream: " << producer_tablet.stream_id
                    << ", tablet: " << producer_tablet.tablet_id
                    << ", colocated table: " << req->table_id();
        }
      }

      // In IMPLICIT mode the from_op_id itself will be the checkpoint.
      OpId commit_op_id = from_op_id;
      if (snapshot_bootstrap) {
        // During snapshot irrespective of IMPLICIT or EXPLICIT mode, we will use the snapshot_op_id
        // as checkpoint.
        commit_op_id = snapshot_op_id;
      } else if (record.GetCheckpointType() == EXPLICIT) {
        commit_op_id = explicit_op_id;
      }

      RPC_STATUS_RETURN_ERROR(
          UpdateCheckpointAndActiveTime(
              producer_tablet, OpId::FromPB(resp->checkpoint().op_id()), commit_op_id,
              last_record_hybrid_time, record.GetSourceType(), snapshot_bootstrap,
              cdc_sdk_safe_time, is_snapshot, snapshot_key,
              (is_snapshot && is_colocated) ? req->table_id() : ""),
          resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
    }

    RPC_STATUS_RETURN_ERROR(
        DoUpdateCDCConsumerOpId(
            tablet_peer, impl_->GetMinSentCheckpointForTablet(req->tablet_id()), req->tablet_id()),
        resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
  }
  // Update relevant GetChanges metrics before handing off the Response.
  UpdateCDCTabletMetrics(
      resp, producer_tablet, tablet_peer, from_op_id, record.GetSourceType(), last_readable_index);

  if (report_tablet_split) {
    RPC_STATUS_RETURN_ERROR(
        impl_->EraseTabletAndStreamEntry(producer_tablet), resp->mutable_error(),
        CDCErrorPB::INTERNAL_ERROR, context);

    SetupErrorAndRespond(
        resp->mutable_error(),
        STATUS(TabletSplit, Format("Tablet Split detected on $0", req->tablet_id())),
        CDCErrorPB::TABLET_SPLIT, &context);
    return;
  }
  if (record.GetSourceType() == XCLUSTER) {
    LOG_SLOW_EXECUTION_EVERY_N_SECS(INFO, 1 /* n_secs */, 100 /* max_expected_millis */,
        Format("Rate limiting GetChanges request for tablet $0", req->tablet_id())) {
      rate_limiter_->Request(resp->ByteSizeLong(), IOPriority::kHigh);
    };
  }
  context.RespondSuccess();
}

Status CDCServiceImpl::UpdatePeersCdcMinReplicatedIndex(
    const TabletId& tablet_id, const TabletCDCCheckpointInfo& cdc_checkpoint_min,
    bool ignore_failures) {
  std::vector<client::internal::RemoteTabletServer*> servers;
  RETURN_NOT_OK(GetTServers(tablet_id, &servers));

  for (const auto& server : servers) {
    if (server->IsLocal()) {
      // We modify our log directly. Avoid calling itself through the proxy.
      continue;
    }
    VLOG(1) << "Modifying remote peer " << server->ToString();
    auto proxy = GetCDCServiceProxy(server);
    UpdateCdcReplicatedIndexRequestPB update_index_req;
    UpdateCdcReplicatedIndexResponsePB update_index_resp;
    update_index_req.add_tablet_ids(tablet_id);
    update_index_req.add_replicated_indices(cdc_checkpoint_min.cdc_op_id.index);
    update_index_req.add_replicated_terms(cdc_checkpoint_min.cdc_op_id.term);
    update_index_req.add_cdc_sdk_safe_times(cdc_checkpoint_min.cdc_sdk_safe_time.ToUint64());
    cdc_checkpoint_min.cdc_sdk_op_id.ToPB(update_index_req.add_cdc_sdk_consumed_ops());
    update_index_req.add_cdc_sdk_ops_expiration_ms(
        cdc_checkpoint_min.cdc_sdk_op_id_expiration.ToMilliseconds());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));
    auto result = proxy->UpdateCdcReplicatedIndex(update_index_req, &update_index_resp, &rpc);

    if (!result.ok() || update_index_resp.has_error()) {
      std::stringstream msg;
      msg << "Failed to update cdc replicated index for tablet: " << tablet_id
          << " in remote peer: " << server->ToString();
      if (update_index_resp.has_error()) {
        msg << ":" << StatusFromPB(update_index_resp.error().status());
      }

      // If UpdateCdcReplicatedIndex failed for one of the tablet peers, don't stop to update
      // the minimum checkpoint to other FOLLOWERs, if ignore_failures is set to 'true'.
      if (ignore_failures) {
        LOG(WARNING) << msg.str();
      } else {
        LOG(ERROR) << msg.str();

        return result.ok() ? STATUS_FORMAT(
                                 InternalError,
                                 "Encountered error: $0 while executing RPC: "
                                 "UpdateCdcReplicatedIndex on Tserver: $1",
                                 update_index_resp.error(), server->ToString())
                           : result;
      }
    }
  }
  return Status::OK();
}

void ComputeLagMetric(
    int64_t last_replicated_micros, int64_t metric_last_timestamp_micros,
    int64_t cdc_state_last_replication_time_micros, scoped_refptr<AtomicGauge<int64_t>> metric) {
  if (metric_last_timestamp_micros == 0) {
    // The tablet metric timestamp is uninitialized, so try to use last replicated time in cdc
    // state.
    if (cdc_state_last_replication_time_micros == 0) {
      // Last replicated time in cdc state is uninitialized as well, so set the metric value to
      // 0 and update later when we have a suitable lower bound.
      metric->set_value(0);
    } else {
      // In the case where no GetChanges request propagates while the producer keeps taking writes,
      // the lag metric will eventually grow large as an indicator of problems.
      int64_t lag_metric = last_replicated_micros - cdc_state_last_replication_time_micros;
      metric->set_value(lag_metric > 0 ? lag_metric : 0);
    }
  } else {
    auto lag_metric = last_replicated_micros - metric_last_timestamp_micros;
    metric->set_value(lag_metric > 0 ? lag_metric : 0);
  }
}

void CDCServiceImpl::ProcessMetricsForEmptyChildrenTablets(
    const EmptyChildrenTabletMap& empty_children_tablets,
    TabletInfoToLastReplicationTimeMap* cdc_state_tablets_to_last_replication_time) {
  // Loop through all the children that are missing metrics. For each child, work up the hierarchy
  // until we find a parent with a valid replication time in cdc_state and use that.
  for (const auto& [child_tablet, child_tablet_meta] : empty_children_tablets) {
    std::optional<uint64_t>* cdc_state_last_repl_time =
        FindOrNull(*cdc_state_tablets_to_last_replication_time, child_tablet);
    uint64_t last_replication_time = 0;

    if (cdc_state_last_repl_time) {
      if (*cdc_state_last_repl_time) {
        // Value was filled by another child tablet earlier, can use that.
        last_replication_time = **cdc_state_last_repl_time;
        continue;
      }

      // Need to work our way up the hierarchy until we find a tablet with a valid value.
      auto parent_tablet = child_tablet_meta.parent_tablet_info;
      std::unordered_set<ProducerTabletInfo, ProducerTabletInfo::Hash> tablet_hierarchy;
      tablet_hierarchy.insert(child_tablet);

      while (!parent_tablet.tablet_id.empty()) {
        cdc_state_last_repl_time =
            FindOrNull(*cdc_state_tablets_to_last_replication_time, parent_tablet);
        if (!cdc_state_last_repl_time) {
          // Could not find an ancestor tablet with a valid time. Set all metrics to 0.
          VLOG(4) << "Could not find valid ancestor tablet for split tablet id "
                  << child_tablet.ToString();
          break;
        }
        // Check if this tablet has a valid time.
        if (*cdc_state_last_repl_time) {
          last_replication_time = **cdc_state_last_repl_time;
          break;
        }
        // Need to continue up the hierarchy, fetch the next parent tablet.
        tablet_hierarchy.insert(parent_tablet);
        auto tablet_res = GetRemoteTablet(parent_tablet.tablet_id);
        if (!tablet_res.ok()) {
          VLOG(4) << "Error when searching for tablet " << parent_tablet.ToString() << " : "
                  << tablet_res;
          break;
        }
        parent_tablet.tablet_id = (*tablet_res)->split_parent_tablet_id();
      }

      // Fill all children tablets with the found value.
      // If last_replication_time == 0 (ie we didn't find the parent), still update the mappings
      // so that we don't try again on other children.
      for (const auto& tablet : tablet_hierarchy) {
        (*cdc_state_tablets_to_last_replication_time)[tablet] = last_replication_time;
      }
    }
    // Update metrics with this value.
    // If last_replication_time == 0, then the final metric will also be set to 0.
    ComputeLagMetric(
        child_tablet_meta.last_replication_time, last_replication_time, 0,
        child_tablet_meta.tablet_metric->async_replication_committed_lag_micros);
    ComputeLagMetric(
        child_tablet_meta.last_replication_time, last_replication_time, 0,
        child_tablet_meta.tablet_metric->async_replication_sent_lag_micros);
  }
}

void CDCServiceImpl::UpdateCDCMetrics() {
  auto tablet_checkpoints = impl_->TabletCheckpointsCopy();
  TabletInfoToLastReplicationTimeMap cdc_state_tablets_to_last_replication_time;
  EmptyChildrenTabletMap empty_children_tablets;

  Status iteration_status;
  auto range_result = cdc_state_table_->GetTableRange(
      CDCStateTableEntrySelector().IncludeLastReplicationTime().IncludeData(), &iteration_status);

  if (!range_result) {
    YB_LOG_EVERY_N_SECS(WARNING, 30) << "Could not update metrics: " << range_result.status();
    return;
  }

  for (auto entry_result : *range_result) {
    if (!entry_result) {
      YB_LOG_EVERY_N_SECS(WARNING, 30) << "Could not update metrics: " << entry_result.status();
      continue;
    }
    const auto& entry = *entry_result;

    // Ignore rows added for colocated tables.
    if (!entry.key.colocated_table_id.empty()) {
      continue;
    }

    // Keep track of all tablets in cdc_state and their last_replication time. This is done to help
    // fill any empty split tablets later, as well as determine what metrics can be cleaned up.
    ProducerTabletInfo tablet_info = {{}, entry.key.stream_id, entry.key.tablet_id};
    cdc_state_tablets_to_last_replication_time.emplace(tablet_info, entry.last_replication_time);

    auto tablet_peer = context_->LookupTablet(entry.key.tablet_id);
    if (!tablet_peer) {
      continue;
    }

    auto get_stream_metadata = GetStream(entry.key.stream_id);
    if (!get_stream_metadata.ok()) {
      continue;
    }
    StreamMetadata& record = **get_stream_metadata;

    bool is_leader = (tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY);

    if (record.GetSourceType() == CDCSDK) {
      auto tablet_metric = std::static_pointer_cast<CDCSDKTabletMetrics>(
          GetCDCTabletMetrics(tablet_info, tablet_peer, record.GetSourceType()));
      if (!tablet_metric) {
        continue;
      }

      // Update the expiry time of for the tablet_id and stream_id combination.
      if (!entry.active_time) {
        tablet_metric->cdcsdk_expiry_time_ms->set_value(
            GetAtomicFlag(&FLAGS_cdc_intent_retention_ms));
      } else {
        int64_t expiry_time =
            *entry.active_time + 1000 * (GetAtomicFlag(&FLAGS_cdc_intent_retention_ms));
        auto now = GetCurrentTimeMicros();
        int64_t remaining_expiry_time_ms = 0;
        if (now < expiry_time) {
          // Convert to milli seconds.
          remaining_expiry_time_ms = (expiry_time - now) / 1000;
        }
        tablet_metric->cdcsdk_expiry_time_ms->set_value(remaining_expiry_time_ms);
      }

      if (!is_leader) {
        tablet_metric->cdcsdk_sent_lag_micros->set_value(0);
      } else {
        auto last_replicated_micros = GetLastReplicatedTime(tablet_peer);
        auto cdc_state_last_replication_time_micros =
            entry.last_replication_time ? *entry.last_replication_time : 0;
        auto last_sent_micros = tablet_metric->cdcsdk_last_sent_physicaltime->value();
        ComputeLagMetric(
            last_replicated_micros, last_sent_micros, cdc_state_last_replication_time_micros,
            tablet_metric->cdcsdk_sent_lag_micros);
      }
    } else {
      // xCluster metrics.
      // Only create the metric if we are the leader.
      auto tablet_metric = std::static_pointer_cast<CDCTabletMetrics>(GetCDCTabletMetrics(
          tablet_info, tablet_peer, record.GetSourceType(), CreateCDCMetricsEntity{is_leader}));
      // If we aren't the leader and have already wiped the metric, can exit early.
      if (!tablet_metric) {
        continue;
      }

      if (!is_leader) {
        // Set all tablet level metrics to 0 since we're not the leader anymore.
        // For certain metrics, such as last_*_physicaltime this leads to us using cdc_state
        // checkpoint values the next time to become leader.
        tablet_metric->ClearMetrics();
        // Also remove this metric metadata so it can be removed by metrics gc.
        RemoveCDCTabletMetrics(tablet_info, tablet_peer);
        continue;
      } else {
        // Get the physical time of the last committed record on producer.
        auto last_replicated_micros = GetLastReplicatedTime(tablet_peer);
        auto cdc_state_last_replication_time_micros =
            entry.last_replication_time ? *entry.last_replication_time : 0;
        auto last_sent_micros = tablet_metric->last_read_physicaltime->value();
        auto last_committed_micros = tablet_metric->last_checkpoint_physicaltime->value();

        if (cdc_state_last_replication_time_micros == 0 && last_sent_micros == 0 &&
            last_committed_micros == 0) {
          auto consensus_res = tablet_peer->GetRaftConsensus();
          if (consensus_res.ok()) {
            const auto& parent_tablet_id = (*consensus_res)->split_parent_tablet_id();
            if (!parent_tablet_id.empty()) {
              // This is a child tablet which has not yet been polled yet, so it still has an empty
              // last_replication_time in cdc_state. In order to update metrics, we will use its
              // parent's last_replication_time.
              ProducerTabletInfo parent_info = {{}, entry.key.stream_id, parent_tablet_id};
              empty_children_tablets.insert(
                  {tablet_info, {parent_info, last_replicated_micros, tablet_metric}});
              continue;
            }
          }
        }

        ComputeLagMetric(
            last_replicated_micros, last_sent_micros, cdc_state_last_replication_time_micros,
            tablet_metric->async_replication_sent_lag_micros);
        ComputeLagMetric(
            last_replicated_micros, last_committed_micros, cdc_state_last_replication_time_micros,
            tablet_metric->async_replication_committed_lag_micros);

        // Time elapsed since last GetChanges, or since stream creation if no GetChanges received.
        // If no GetChanges received and creation time uninitialized, do not update the metric.
        auto last_getchanges_time = tablet_metric->last_getchanges_time->value();
        if (last_getchanges_time || cdc_state_last_replication_time_micros) {
          last_getchanges_time = last_getchanges_time == 0 ? cdc_state_last_replication_time_micros
                                                           : last_getchanges_time;
          tablet_metric->time_since_last_getchanges->set_value(
              GetCurrentTimeMicros() - last_getchanges_time);
        }
      }
    }
  }

  if (!iteration_status.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 30) << "Could not update metrics: " << iteration_status;
    return;
  }

  // Process metrics for the children tablets that do not have last_replication_time.
  ProcessMetricsForEmptyChildrenTablets(
      empty_children_tablets, &cdc_state_tablets_to_last_replication_time);

  // Now, go through tablets in tablet_checkpoints_ and set lag to 0 for all tablets we're no
  // longer replicating.
  for (const auto& checkpoint : tablet_checkpoints) {
    const ProducerTabletInfo& tablet_info = checkpoint.producer_tablet_info;
    if (!cdc_state_tablets_to_last_replication_time.contains(tablet_info)) {
      // We're no longer replicating this tablet, so set lag to 0.
      auto tablet_peer = context_->LookupTablet(checkpoint.tablet_id());
      if (!tablet_peer) {
        continue;
      }
      auto get_stream_metadata = GetStream(checkpoint.producer_tablet_info.stream_id);
      if (!get_stream_metadata.ok()) {
        continue;
      }
      StreamMetadata& record = **get_stream_metadata;

      // Don't create new tablet metrics if they have already been deleted.
      auto tablet_metric_row = GetCDCTabletMetrics(
          checkpoint.producer_tablet_info, tablet_peer, record.GetSourceType(),
          CreateCDCMetricsEntity::kFalse);
      if (!tablet_metric_row) {
        continue;
      }
      if (record.GetSourceType() == CDCSDK) {
        auto tablet_metric = std::static_pointer_cast<CDCSDKTabletMetrics>(tablet_metric_row);
        tablet_metric->cdcsdk_sent_lag_micros->set_value(0);
        tablet_metric->cdcsdk_traffic_sent.reset();
        tablet_metric->cdcsdk_change_event_count.reset();
        tablet_metric->cdcsdk_expiry_time_ms->set_value(0);
      } else {
        auto tablet_metric = std::static_pointer_cast<CDCTabletMetrics>(tablet_metric_row);
        tablet_metric->ClearMetrics();
      }
      RemoveCDCTabletMetrics(checkpoint.producer_tablet_info, tablet_peer);
    }
  }
}

bool CDCServiceImpl::ShouldUpdateCDCMetrics(MonoTime time_of_last_update_metrics) {
  // Only update metrics if cdc is enabled, which means we have a valid replication stream.
  if (!GetAtomicFlag(&FLAGS_enable_collect_cdc_metrics)) {
    return false;
  }
  if (time_of_last_update_metrics == MonoTime::kUninitialized) {
    return true;
  }

  const auto delta_since_last_update = MonoTime::Now() - time_of_last_update_metrics;
  const auto update_interval_ms = GetAtomicFlag(&FLAGS_update_metrics_interval_ms);
  // Only log warning message if it has been more than 4 times the metrics update interval.
  // By default, this is 15s * 4 = 1 minute.
  if (delta_since_last_update >= update_interval_ms * 4ms) {
    YB_LOG_EVERY_N_SECS(WARNING, 300)
        << "UpdateCDCMetrics was delayed by " << delta_since_last_update << THROTTLE_MSG;
  }
  return delta_since_last_update >= MonoDelta::FromMilliseconds(update_interval_ms);
}

bool CDCServiceImpl::CDCEnabled() { return cdc_enabled_.load(std::memory_order_acquire); }

void CDCServiceImpl::SetCDCServiceEnabled() { cdc_enabled_.store(true, std::memory_order_release); }

void CDCServiceImpl::SetPausedXClusterProducerStreams(
    const ::google::protobuf::Map<std::string, bool>& paused_producer_stream_ids,
    uint32_t xcluster_config_version) {
  std::lock_guard l(mutex_);
  if (xcluster_config_version_ < xcluster_config_version) {
    paused_xcluster_producer_streams_.clear();
    for (const auto& stream_id : paused_producer_stream_ids) {
      paused_xcluster_producer_streams_.insert(stream_id.first);
    }
    xcluster_config_version_ = xcluster_config_version;
    const auto list_str = JoinStrings(paused_xcluster_producer_streams_, ",");
    LOG(INFO) << "Updating xCluster paused producer streams: " << list_str
              << " Config version: " << xcluster_config_version_;
  }
}

uint32_t CDCServiceImpl::GetXClusterConfigVersion() const {
  SharedLock<rw_spinlock> l(mutex_);
  return xcluster_config_version_;
}

MicrosTime CDCServiceImpl::GetLastReplicatedTime(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer) {
  tablet::RemoveIntentsData data;
  auto status = tablet_peer->GetLastReplicatedData(&data);
  return status.ok() ? data.log_ht.GetPhysicalValueMicros() : 0;
}

void SetMinCDCSDKCheckpoint(const OpId& checkpoint, OpId* cdc_sdk_op_id) {
  if (*cdc_sdk_op_id != OpId::Invalid()) {
    *cdc_sdk_op_id = min(*cdc_sdk_op_id, checkpoint);
  } else {
    *cdc_sdk_op_id = checkpoint;
  }
}

void SetMinCDCSDKSafeTime(const HybridTime& cdc_sdk_safe_time, HybridTime* cdc_sdk_min_safe_time) {
  (*cdc_sdk_min_safe_time).MakeAtMost(cdc_sdk_safe_time);
}

void PopulateTabletMinCheckpointAndLatestActiveTime(
    const string& tablet_id, const OpId& checkpoint, CDCRequestSource cdc_source_type,
    const int64_t& last_active_time, TabletIdCDCCheckpointMap* tablet_min_checkpoint_index,
    const HybridTime cdc_sdk_safe_time = HybridTime::kInvalid) {
  auto& tablet_info = (*tablet_min_checkpoint_index)[tablet_id];

  tablet_info.cdc_op_id = min(tablet_info.cdc_op_id, checkpoint);
  // Case:1  2 different CDCSDK stream(stream-1 and stream-2) on same tablet_id.
  //        for stream-1 there is get changes call and stream-2 there is not get change
  //        call(i.e initial checkpoint is -1.-1).
  //
  // Case:2 for the same tablet_id we read CDC stream-1 and we set cdc_sdk_op_id = Invalid(-1.-1)
  //       then we read CDCSDK stream-2 which have valid checkpoint detail in cdc_state table,
  //       update cdc_sdk_op_id to checkpoint.
  //
  if (cdc_source_type == CDCSDK) {
    SetMinCDCSDKCheckpoint(checkpoint, &tablet_info.cdc_sdk_op_id);
    SetMinCDCSDKSafeTime(cdc_sdk_safe_time, &tablet_info.cdc_sdk_safe_time);
    tablet_info.cdc_sdk_latest_active_time =
        max(tablet_info.cdc_sdk_latest_active_time, last_active_time);
  }
}

Status CDCServiceImpl::SetInitialCheckPoint(
    const OpId& checkpoint, const string& tablet_id,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, HybridTime cdc_sdk_safe_time) {
  VLOG(1) << "Setting the checkpoint is " << checkpoint.ToString()
          << " and the latest entry OpID is " << tablet_peer->log()->GetLatestEntryOpId()
          << " for tablet_id: " << tablet_id;
  auto result = PopulateTabletCheckPointInfo(tablet_id);
  RETURN_NOT_OK_SET_CODE(result, CDCError(CDCErrorPB::INTERNAL_ERROR));
  TabletIdCDCCheckpointMap& tablet_min_checkpoint_map = *result;
  auto& tablet_op_id = tablet_min_checkpoint_map[tablet_id];
  SetMinCDCSDKSafeTime(cdc_sdk_safe_time, &tablet_op_id.cdc_sdk_safe_time);
  SetMinCDCSDKCheckpoint(checkpoint, &tablet_op_id.cdc_sdk_op_id);
  tablet_op_id.cdc_sdk_op_id_expiration =
      MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms));

  // Update the minimum checkpoint op_id on LEADER for log cache eviction for all stream type.
  RETURN_NOT_OK_SET_CODE(
      DoUpdateCDCConsumerOpId(tablet_peer, tablet_op_id.cdc_op_id, tablet_id),
      CDCError(CDCErrorPB::INTERNAL_ERROR));

  // Update the minimum checkpoint op_id for LEADER for intent cleanup for CDCSDK Stream type.
  RETURN_NOT_OK_SET_CODE(
      tablet_peer->SetCDCSDKRetainOpIdAndTime(
          tablet_op_id.cdc_sdk_op_id, tablet_op_id.cdc_sdk_op_id_expiration,
          tablet_op_id.cdc_sdk_safe_time),
      CDCError(CDCErrorPB::INTERNAL_ERROR));

  //  Even if the flag is enable_update_local_peer_min_index is set, for the first time
  //  we need to set it to follower too.
  return UpdatePeersCdcMinReplicatedIndex(tablet_id, tablet_op_id, false);
}

void CDCServiceImpl::FilterOutTabletsToBeDeletedByAllStreams(
    TabletIdCDCCheckpointMap* tablet_checkpoint_map,
    std::unordered_set<TabletId>* tablet_ids_with_max_checkpoint) {
  for (auto iter = tablet_checkpoint_map->begin(); iter != tablet_checkpoint_map->end();) {
    if (iter->second.cdc_sdk_op_id == OpId::Max()) {
      tablet_ids_with_max_checkpoint->insert(iter->first);
      iter = tablet_checkpoint_map->erase(iter);
    } else {
      ++iter;
    }
  }
}

Result<TabletIdCDCCheckpointMap> CDCServiceImpl::PopulateTabletCheckPointInfo(
    const TabletId& input_tablet_id, TabletIdStreamIdSet* tablet_stream_to_be_deleted) {
  TabletIdCDCCheckpointMap tablet_min_checkpoint_map;

  int count = 0;
  Status iteration_status;
  auto table_range = VERIFY_RESULT(cdc_state_table_->GetTableRange(
      CDCStateTableEntrySelector().IncludeCheckpoint().IncludeLastReplicationTime().IncludeData(),
      &iteration_status));
  for (auto entry_result : table_range) {
    if (!entry_result) {
      LOG(WARNING) << "Populate tablet checkpoint failed for row. " << entry_result.status();
      continue;
    }

    const auto& entry = *entry_result;

    // We ignore rows added for colocated tables.
    if (!entry.key.colocated_table_id.empty()) {
      continue;
    }

    const auto& stream_id = entry.key.stream_id;
    const auto& tablet_id = entry.key.tablet_id;
    const auto& checkpoint = *entry.checkpoint;
    count++;

    // Find the minimum checkpoint op_id per tablet. This minimum op_id
    // will be passed to LEADER and it's peers for log cache eviction and clean the consumed intents
    // in a regular interval.
    if (!input_tablet_id.empty() && input_tablet_id != tablet_id) {
      continue;
    }

    std::string last_replicated_time_str = "";
    if (entry.last_replication_time) {
      last_replicated_time_str = Timestamp(*entry.last_replication_time).ToFormattedString();
    }

    auto get_stream_metadata = GetStream(stream_id);
    if (!get_stream_metadata.ok()) {
      LOG(WARNING) << "Read invalid stream id: " << stream_id << " for tablet " << tablet_id << ": "
                   << get_stream_metadata.status();
      // The stream_id present in the cdc_state table was not found in the master cache, it means
      // that the stream is deleted. To update the corresponding tablet PEERs, give an entry in
      // tablet_min_checkpoint_map which will update  cdc_sdk_min_checkpoint_op_id to
      // OpId::Max()(i.e no need to retain the intents.). And also mark the row to be deleted.
      if (!tablet_min_checkpoint_map.contains(tablet_id)) {
        VLOG(2) << "We could not get the metadata for the stream: " << stream_id;
        auto& tablet_info = tablet_min_checkpoint_map[tablet_id];
        tablet_info.cdc_op_id = OpId::Max();
        tablet_info.cdc_sdk_op_id = OpId::Max();
        tablet_info.cdc_sdk_safe_time = HybridTime::kInvalid;
      }
      if (get_stream_metadata.status().IsNotFound()) {
        VLOG(2) << "We will remove the entry for the stream: " << stream_id
                << ", from cdc_state table.";
        tablet_stream_to_be_deleted->insert({tablet_id, stream_id});
        RemoveStreamFromCache(stream_id);
      }
      continue;
    }
    StreamMetadata& record = **get_stream_metadata;

    HybridTime cdc_sdk_safe_time = HybridTime::kInvalid;
    int64_t last_active_time_cdc_state_table = std::numeric_limits<int64_t>::min();
    // We will only populate the "cdc_sdk_safe_time" when before image is active or when we are in
    // taking the snapshot of any table.
    if (entry.cdc_sdk_safe_time &&
        (record.GetRecordType() == CDCRecordType::ALL || entry.snapshot_key.has_value())) {
      cdc_sdk_safe_time = HybridTime(*entry.cdc_sdk_safe_time);
    }

    if (entry.active_time) {
      last_active_time_cdc_state_table = *entry.active_time;
    }

    VLOG(1) << "stream_id: " << stream_id << ", tablet_id: " << tablet_id
            << ", checkpoint: " << checkpoint
            << ", last replicated time: " << last_replicated_time_str
            << ", last active time: " << last_active_time_cdc_state_table
            << ", cdc_sdk_safe_time: " << cdc_sdk_safe_time;

    // Add the {tablet_id, stream_id} pair to the set if its checkpoint is OpId::Max().
    if (tablet_stream_to_be_deleted && checkpoint == OpId::Max()) {
      tablet_stream_to_be_deleted->insert({tablet_id, stream_id});
    }

    // If a tablet_id, stream_id pair is in "uninitialized state", we don't need to send the
    // checkpoint to the tablet peers.
    if (checkpoint == OpId::Invalid() && last_active_time_cdc_state_table == 0) {
      continue;
    }

    // If the tablet_id, stream_id pair have OpId::Min() as checkpoint, but the LastReplicatedTime
    // is not set, we know this was a child tablet (refer: UpdateCDCProducerOnTabletSplit). We will
    // not update the checkpoint details.
    if (checkpoint == OpId::Min() && last_replicated_time_str.empty() &&
        record.GetSourceType() == CDCSDK) {
      continue;
    }

    // Check that requested tablet_id is part of the CDC stream.
    ProducerTabletInfo producer_tablet = {{}, stream_id, tablet_id};

    // Check stream associated with the tablet is active or not.
    // Don't consider those inactive stream for the min_checkpoint calculation.
    int64_t latest_active_time = 0;
    if (record.GetSourceType() == CDCSDK) {
      // Support backward compatibility, where active_time as not part of cdc_state table.
      if (last_active_time_cdc_state_table == std::numeric_limits<int64_t>::min()) {
        LOG(WARNING)
            << "In previous server version, active time was not part of cdc_state table,"
               "as a part of upgrade, updating the active time forcefully for the tablet_id: "
            << tablet_id;
        last_active_time_cdc_state_table = GetCurrentTimeMicros();
      }
      auto status = CheckStreamActive(producer_tablet, last_active_time_cdc_state_table);
      if (!status.ok()) {
        // It is possible that all streams associated with a tablet have expired, in which case we
        // have to create a default entry in 'tablet_min_checkpoint_map' corresponding to the
        // tablet. This way the fact that all the streams have expired will be communicated to the
        // tablet_peer as well, through the method: "UpdateTabletPeersWithMinReplicatedIndex". If
        // 'tablet_min_checkpoint_map' already had an entry corresponding to the tablet, then
        // either we already saw an inactive stream assocaited with the tablet and created the
        // default entry or we saw an active stream and the map has a legitimate entry, in both
        // cases repopulating the map is not needed.
        if (tablet_min_checkpoint_map.find(tablet_id) == tablet_min_checkpoint_map.end()) {
          VLOG(2) << "Stream: " << stream_id << ", is expired for tablet: " << tablet_id
                  << ", hence we are adding default entries to tablet_min_checkpoint_map";
          auto& tablet_info = tablet_min_checkpoint_map[tablet_id];
          tablet_info.cdc_sdk_op_id = OpId::Max();
          tablet_info.cdc_sdk_safe_time = HybridTime::kInvalid;
        }
        continue;
      }
      latest_active_time = last_active_time_cdc_state_table;
    }

    // Ignoring those non-bootstarped CDCSDK stream
    if (checkpoint != OpId::Invalid()) {
      PopulateTabletMinCheckpointAndLatestActiveTime(
          tablet_id, checkpoint, record.GetSourceType(), latest_active_time,
          &tablet_min_checkpoint_map, cdc_sdk_safe_time);
    }
  }

  RETURN_NOT_OK(iteration_status);

  YB_LOG_EVERY_N_SECS(INFO, 300) << "Read " << count << " records from "
                                 << kCdcStateTableName;
  return tablet_min_checkpoint_map;
}

void CDCServiceImpl::UpdateTabletPeersWithMaxCheckpoint(
    const std::unordered_set<TabletId>& tablet_ids_with_max_checkpoint,
    std::unordered_set<TabletId>* failed_tablet_ids) {
  TabletCDCCheckpointInfo tablet_info;
  tablet_info.cdc_sdk_op_id = OpId::Max();
  tablet_info.cdc_op_id = OpId::Max();
  tablet_info.cdc_sdk_latest_active_time = 0;

  for (const auto& tablet_id : tablet_ids_with_max_checkpoint) {
    // When a CDCSDK Stream is deleted the row will be marked for deletion with OpId::Max(). All
    // such rows are collected here. We will try set the CDCSDK checkpoint as OpId::Max in all the
    // tablet peers by sending RPCs , and only if they all succeeded we will delete the
    // corresponding row from 'cdc_state' table. To ensure the OpId::Max() is set in all tablet
    // peers before we delete the row from 'cdc_state' table, we are passing
    // 'enable_update_local_peer_min_index' as false.
    auto s = UpdateTabletPeerWithCheckpoint(
        tablet_id, &tablet_info, false /* enable_update_local_peer_min_index */,
        false /* ignore_rpc_failures */);

    if (!s.ok()) {
      failed_tablet_ids->insert(tablet_id);
      VLOG(1) << "Could not successfully update checkpoint as 'OpId::Max' for tablet: " << tablet_id
              << ", on all tablet peers";
    }
  }
}

void CDCServiceImpl::UpdateTabletPeersWithMinReplicatedIndex(
    TabletIdCDCCheckpointMap* tablet_min_checkpoint_map) {
  auto enable_update_local_peer_min_index =
      GetAtomicFlag(&FLAGS_enable_update_local_peer_min_index);

  for (auto& [tablet_id, tablet_info] : *tablet_min_checkpoint_map) {
    auto s =
        UpdateTabletPeerWithCheckpoint(tablet_id, &tablet_info, enable_update_local_peer_min_index);
  }
}

Status CDCServiceImpl::UpdateTabletPeerWithCheckpoint(
    const TabletId& tablet_id, TabletCDCCheckpointInfo* tablet_info,
    bool enable_update_local_peer_min_index, bool ignore_rpc_failures) {
  auto tablet_peer_result = context_->GetTablet(tablet_id);
  if (!tablet_peer_result.ok()) {
    if (tablet_peer_result.status().IsNotFound()) {
      VLOG(2) << "Did not find tablet peer for tablet " << tablet_id;
    } else {
      LOG(WARNING) << "Error getting tablet_peer for tablet " << tablet_id << ": "
                   << tablet_peer_result.status();
    }
    return STATUS_FORMAT(NotFound, "Tablet peer not found");
  }

  auto tablet_peer = std::move(*tablet_peer_result);
  if (!enable_update_local_peer_min_index && !tablet_peer->IsLeaderAndReady()) {
    VLOG(2) << "Tablet peer " << tablet_peer->permanent_uuid() << " is not the leader for tablet "
            << tablet_id;
    return STATUS_FORMAT(InternalError, "Current TServer does not host leader");
  }

  auto min_index = tablet_info->cdc_op_id.index;
  auto current_term = tablet_info->cdc_op_id.term;
  auto s = tablet_peer->set_cdc_min_replicated_index(min_index);
  WARN_NOT_OK(
      s, Format(
             "Unable to set cdc min index for tablet $0 peer $1", tablet_peer->tablet_id(),
             tablet_peer->permanent_uuid()));
  RETURN_NOT_OK(s);

  auto result = tablet_peer->GetCDCSDKIntentRetainTime(tablet_info->cdc_sdk_latest_active_time);
  WARN_NOT_OK(
      result, "Unable to get the intent retain time for tablet peer " +
                  tablet_peer->permanent_uuid() + ", and tablet " + tablet_peer->tablet_id());
  RETURN_NOT_OK(result);
  tablet_info->cdc_sdk_op_id_expiration = *result;

  if (!enable_update_local_peer_min_index) {
    VLOG(1) << "Updating followers for tablet " << tablet_id << " with index " << min_index
            << " term " << current_term
            << " cdc_sdk_op_id: " << tablet_info->cdc_sdk_op_id.ToString()
            << " expiration: " << tablet_info->cdc_sdk_op_id_expiration.ToMilliseconds()
            << " cdc_sdk_safe_time: " << tablet_info->cdc_sdk_safe_time;
    s = UpdatePeersCdcMinReplicatedIndex(tablet_id, *tablet_info, ignore_rpc_failures);
    WARN_NOT_OK(s, "UpdatePeersCdcMinReplicatedIndex failed");
    if (!ignore_rpc_failures && !s.ok()) {
      return s;
    }
  } else {
    s = tablet_peer->SetCDCSDKRetainOpIdAndTime(
        tablet_info->cdc_sdk_op_id, tablet_info->cdc_sdk_op_id_expiration,
        tablet_info->cdc_sdk_safe_time);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to set CDCSDK min checkpoint for tablet peer "
                   << tablet_peer->permanent_uuid() << " and tablet " << tablet_peer->tablet_id()
                   << ": " << s;
      return s;
    }
  }

  return Status::OK();
}

Status CDCServiceImpl::GetTabletIdsToPoll(
    const xrepl::StreamId stream_id,
    const std::set<TabletId>& active_or_hidden_tablets,
    const std::set<TabletId>& parent_tablets,
    const std::map<TabletId, TabletId>& child_to_parent_mapping,
    std::vector<std::pair<TabletId, CDCSDKCheckpointPB>>* result) {
  std::map<TabletId, uint> parent_to_polled_child_count;
  std::set<TabletId> polled_tablets;

  Status iteration_status;
  auto entries = VERIFY_RESULT(cdc_state_table_->GetTableRange(
      CDCStateTableEntrySelector()
          .IncludeCheckpoint()
          .IncludeLastReplicationTime()
          .IncludeCDCSDKSafeTime(),
      &iteration_status));

  for (auto entry_result : entries) {
    if (!entry_result) {
      LOG(WARNING) << "GetTabletIdsToPoll failed to parse row :" << entry_result.status();
      continue;
    }
    auto& entry = *entry_result;
    if (entry.key.stream_id != stream_id) {
      continue;
    }

    auto& tablet_id = entry.key.tablet_id;
    auto is_cur_tablet_polled = entry.last_replication_time.has_value();
    if (!is_cur_tablet_polled) {
      continue;
    }

    polled_tablets.insert(tablet_id);

    auto iter = child_to_parent_mapping.find(tablet_id);
    if (iter != child_to_parent_mapping.end()) {
      parent_to_polled_child_count[iter->second] += 1;
    }
  }
  RETURN_NOT_OK(iteration_status);

  for (const auto& entry_result : entries) {
    if (!entry_result) {
      LOG(WARNING) << "GetTabletIdsToPoll failed to parse row :" << entry_result.status();
      continue;
    }

    auto& entry = *entry_result;
    if (entry.key.stream_id != stream_id) {
      continue;
    }

    auto& tablet_id = entry.key.tablet_id;
    auto is_active_or_hidden =
        (active_or_hidden_tablets.find(tablet_id) != active_or_hidden_tablets.end());
    if (!is_active_or_hidden) {
      // This means the row is for a child tablet for which split is initiated but the process is
      // not complete.
      continue;
    }

    auto is_parent = (parent_tablets.find(tablet_id) != parent_tablets.end());

    auto are_both_children_polled = [&](const TabletId& tablet_id) {
      auto iter = parent_to_polled_child_count.find(tablet_id);
      if (iter != parent_to_polled_child_count.end() && iter->second == 2) {
        return true;
      }
      return false;
    };

    CDCSDKCheckpointPB checkpoint_pb;
    checkpoint_pb.set_term(entry.checkpoint->term);
    checkpoint_pb.set_index(entry.checkpoint->index);
    if (entry.cdc_sdk_safe_time.has_value()) {
      checkpoint_pb.set_snapshot_time(*entry.cdc_sdk_safe_time);
    }

    auto is_cur_tablet_polled = entry.last_replication_time.has_value();

    bool add_to_result = false;
    auto parent_iter = child_to_parent_mapping.find(tablet_id);

    if (is_parent) {
      // This means the current tablet itself was a parent tablet. If we find
      // that we have already started polling both the children, we will not add the parent tablet
      // to the result set. This situation is only possible within a small window where we have
      // reported the tablet split to the client and but the background thread has not yet deleted
      // the hidden parent tablet.
      bool both_children_polled = are_both_children_polled(tablet_id);

      if (!both_children_polled) {
        // This can occur in two scenarios:
        // 1. The client has just called "GetTabletListToPollForCDC" for the first time, meanwhile
        //    a tablet split has succeded. In this case we will only add the children tablets to
        //    the result.
        // 2. The client has not yet completed streaming all the required data from the current
        //    parent tablet. In this case we will only add the current tablet to the result.
        // The difference between the two scenarios is that the current parent tablet has been
        // polled.
        if (is_cur_tablet_polled) {
          add_to_result = true;
        }
        VLOG_IF(1, !is_cur_tablet_polled)
            << "The current tablet: " << tablet_id
            << ", has children tablets and hasn't been polled yet. The CDC stream: " << stream_id
            << ", can directly start polling from children tablets.";
      }
    } else if (parent_iter == child_to_parent_mapping.end()) {
      // This means the current tablet is not a child tablet, nor a parent, we add the tablet to the
      // result set.
      add_to_result = true;
    } else {
      // This means the current tablet is a child tablet.If we see that the any ancestor tablet is
      // also not polled we will add the current child tablet to the result set.
      bool found_active_ancestor = false;
      while (parent_iter != child_to_parent_mapping.end()) {
        const auto& ancestor_tablet_id = parent_iter->second;

        bool both_children_polled = are_both_children_polled(ancestor_tablet_id);
        bool is_current_polled = (polled_tablets.find(ancestor_tablet_id) != polled_tablets.end());
        if (is_current_polled && !both_children_polled) {
          VLOG(1) << "Found polled ancestor tablet: " << ancestor_tablet_id
                  << ", for un-polled child tablet: " << tablet_id
                  << ". Hence this tablet is not yet ready to be polled by CDC stream: "
                  << stream_id;
          found_active_ancestor = true;
          break;
        }

        // Get the iter to the parent of the current tablet.
        parent_iter = child_to_parent_mapping.find(ancestor_tablet_id);
      }

      if (!found_active_ancestor) {
        add_to_result = true;
      }
    }

    if (add_to_result) {
      result->push_back(std::make_pair(tablet_id, checkpoint_pb));
    }
  }

  return iteration_status;
}

void CDCServiceImpl::UpdatePeersAndMetrics() {
  MonoTime time_since_update_peers = MonoTime::kUninitialized;
  MonoTime time_since_update_metrics = MonoTime::kUninitialized;

  // Returns false if the CDC service has been stopped.
  auto sleep_while_not_stopped = [this]() {
    int min_sleep_ms = std::min(100, GetAtomicFlag(&FLAGS_update_metrics_interval_ms));
    auto sleep_period = MonoDelta::FromMilliseconds(min_sleep_ms);
    SleepFor(sleep_period);

    SharedLock<decltype(mutex_)> l(mutex_);
    return !cdc_service_stopped_;
  };

  do {
    if (!cdc_enabled_.load(std::memory_order_acquire)) {
      // CDC service not enabled, so skip background thread work.
      continue;
    }
    // Should we update lag metrics default every 1s.
    if (ShouldUpdateCDCMetrics(time_since_update_metrics)) {
      UpdateCDCMetrics();
      time_since_update_metrics = MonoTime::Now();
    }

    // If its not been 60s since the last peer update, continue.
    if (!GetAtomicFlag(&FLAGS_enable_log_retention_by_op_idx) ||
        (time_since_update_peers != MonoTime::kUninitialized &&
         MonoTime::Now() - time_since_update_peers <
             MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_update_min_cdc_indices_interval_secs)))) {
      continue;
    }
    time_since_update_peers = MonoTime::Now();
    VLOG(2) << "Updating tablet peers with min cdc replicated index";
    {
      YB_LOG_EVERY_N_SECS(INFO, 300)
          << "Started to read minimum replicated indices for all tablets";
    }
    // Don't exit from this thread even if below method throw error, because
    // if we fail to read cdc_state table, lets wait for the next retry after 60 secs.
    TabletIdStreamIdSet cdc_state_entries_to_delete;
    auto result = PopulateTabletCheckPointInfo("", &cdc_state_entries_to_delete);
    if (!result.ok()) {
      LOG(WARNING) << "Failed to populate tablets checkpoint info: " << result.status();
      continue;
    }
    TabletIdCDCCheckpointMap& tablet_checkpoint_map = *result;
    VLOG(3) << "List of tablets with checkpoint info read from cdc_state table: "
            << tablet_checkpoint_map.size();

    // Collect and remove entries for the tablet_ids for which we will set the checkpoint as
    // 'OpId::Max' from 'tablet_checkpoint_map', into 'tablet_ids_with_max_checkpoint'.
    std::unordered_set<TabletId> tablet_ids_with_max_checkpoint;
    FilterOutTabletsToBeDeletedByAllStreams(
        &tablet_checkpoint_map, &tablet_ids_with_max_checkpoint);

    UpdateTabletPeersWithMinReplicatedIndex(&tablet_checkpoint_map);

    {
      YB_LOG_EVERY_N_SECS(INFO, 300)
          << "Done reading all the indices for all tablets and updating peers";
    }

    std::unordered_set<TabletId> failed_tablet_ids;
    UpdateTabletPeersWithMaxCheckpoint(tablet_ids_with_max_checkpoint, &failed_tablet_ids);

    WARN_NOT_OK(
        DeleteCDCStateTableMetadata(cdc_state_entries_to_delete, failed_tablet_ids),
        "Unable to cleanup CDC State table metadata");

    rate_limiter_->SetBytesPerSecond(
        GetAtomicFlag(&FLAGS_xcluster_get_changes_max_send_rate_mbps) * 1_MB);

  } while (sleep_while_not_stopped());
}

Status CDCServiceImpl::DeleteCDCStateTableMetadata(
    const TabletIdStreamIdSet& cdc_state_entries_to_delete,
    const std::unordered_set<TabletId>& failed_tablet_ids) {
  // Iterating over set and deleting entries from the cdc_state table.
  for (const auto& [tablet_id, stream_id] : cdc_state_entries_to_delete) {
    if (failed_tablet_ids.contains(tablet_id)) {
      VLOG(2) << "We cannot delete the entry for the tablet: " << tablet_id
              << ", from cdc_state table yet. Since we encounterted failures while "
                 "propogating the checkpoint of OpId::Max to all the tablet peers";
      continue;
    }
    auto tablet_peer_result = context_->GetServingTablet(tablet_id);
    if (!tablet_peer_result.ok()) {
      LOG(WARNING) << "Could not delete the entry for stream" << stream_id << " and the tablet "
                   << tablet_id;
      continue;
    }
    if ((*tablet_peer_result)->IsLeaderAndReady()) {
      Status s = cdc_state_table_->DeleteEntries({{tablet_id, stream_id}});
      if (!s.ok()) {
        LOG(WARNING) << "Unable to flush operations to delete cdc streams: " << s;
        return s.CloneAndPrepend("Error deleting cdc stream rows from cdc_state table");
      }
      LOG(INFO) << "CDC state table entry for tablet " << tablet_id << " and streamid " << stream_id
                << " is deleted";
    }
  }
  return Status::OK();
}

Result<client::internal::RemoteTabletPtr> CDCServiceImpl::GetRemoteTablet(
    const TabletId& tablet_id, const bool use_cache) {
  std::promise<Result<client::internal::RemoteTabletPtr>> tablet_lookup_promise;
  auto future = tablet_lookup_promise.get_future();
  auto callback =
      [&tablet_lookup_promise](const Result<client::internal::RemoteTabletPtr>& result) {
        tablet_lookup_promise.set_value(result);
      };

  auto start = CoarseMonoClock::Now();
  client()->LookupTabletById(
      tablet_id,
      /* table =*/nullptr,
      // In case this is a split parent tablet, it will be hidden so we need this flag to access it.
      master::IncludeInactive::kTrue,
      master::IncludeDeleted::kFalse,
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms),
      callback,
      GetAtomicFlag(&FLAGS_enable_cdc_client_tablet_caching) && use_cache
          ? client::UseCache::kTrue
          : client::UseCache::kFalse);
  future.wait();

  auto duration = CoarseMonoClock::Now() - start;
  if (duration > (kMaxDurationForTabletLookup * 1ms)) {
    LOG(WARNING) << "LookupTabletByKey took long time: " << AsString(duration);
  }

  auto remote_tablet = VERIFY_RESULT(future.get());
  return remote_tablet;
}

Result<RemoteTabletServer*> CDCServiceImpl::GetLeaderTServer(
    const TabletId& tablet_id, const bool use_cache) {
  auto result = VERIFY_RESULT(GetRemoteTablet(tablet_id, use_cache));

  auto ts = result->LeaderTServer();
  if (ts == nullptr) {
    return STATUS(NotFound, "Tablet leader not found for tablet", tablet_id);
  }
  return ts;
}

Status CDCServiceImpl::GetTServers(
    const TabletId& tablet_id, std::vector<client::internal::RemoteTabletServer*>* servers) {
  auto result = VERIFY_RESULT(GetRemoteTablet(tablet_id));

  result->GetRemoteTabletServers(servers);
  return Status::OK();
}

std::shared_ptr<CDCServiceProxy> CDCServiceImpl::GetCDCServiceProxy(RemoteTabletServer* ts) {
  auto hostport = HostPortFromPB(ts->DesiredHostPort(client()->cloud_info()));
  DCHECK(!hostport.host().empty());

  {
    SharedLock<decltype(mutex_)> l(mutex_);
    auto it = cdc_service_map_.find(hostport);
    if (it != cdc_service_map_.end()) {
      return it->second;
    }
  }

  auto cdc_service = std::make_shared<CDCServiceProxy>(&client()->proxy_cache(), hostport);

  {
    std::lock_guard l(mutex_);
    auto it = cdc_service_map_.find(hostport);
    if (it != cdc_service_map_.end()) {
      return it->second;
    }
    cdc_service_map_.emplace(hostport, cdc_service);
  }
  return cdc_service;
}

void CDCServiceImpl::TabletLeaderGetChanges(
    const GetChangesRequestPB* req,
    GetChangesResponsePB* resp,
    std::shared_ptr<RpcContext>
        context,
    std::shared_ptr<tablet::TabletPeer>
        peer) {
  auto rpc_handle = rpcs_.Prepare();
  RPC_CHECK_AND_RETURN_ERROR(
      rpc_handle != rpcs_.InvalidHandle(),
      STATUS(
          Aborted,
          Format(
              "Could not create valid handle for GetChangesRpc: tablet=$0, peer=$1",
              req->tablet_id(),
              peer->permanent_uuid())),
      resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, *context.get());

  // Increment Proxy Metric.
  server_metrics_->cdc_rpc_proxy_count->Increment();

  // Forward this Request Info to the proper TabletServer.
  GetChangesRequestPB new_req;
  new_req.CopyFrom(*req);
  new_req.set_serve_as_proxy(false);
  CoarseTimePoint deadline = GetDeadline(*context, client());

  *rpc_handle = rpc::xcluster::CreateGetChangesRpc(
      deadline, nullptr, /* RemoteTablet: will get this from 'new_req' */
      client(), &new_req,
      [this, resp, context, rpc_handle](const Status& status, GetChangesResponsePB&& new_resp) {
        auto retained = rpcs_.Unregister(rpc_handle);
        *resp = std::move(new_resp);
        RPC_STATUS_RETURN_ERROR(status, resp->mutable_error(), resp->error().code(), *context);
        context->RespondSuccess();
      });
  (**rpc_handle).SendRpc();
}

void CDCServiceImpl::TabletLeaderGetCheckpoint(
    const GetCheckpointRequestPB* req, GetCheckpointResponsePB* resp, RpcContext* context) {
  auto ts_leader = RPC_VERIFY_RESULT(
      GetLeaderTServer(req->tablet_id(), false /* use_cache */), resp->mutable_error(),
      CDCErrorPB::TABLET_NOT_FOUND, *context);

  auto cdc_proxy = GetCDCServiceProxy(ts_leader);
  rpc::RpcController rpc;
  rpc.set_deadline(GetDeadline(*context, client()));

  GetCheckpointRequestPB new_req;
  new_req.CopyFrom(*req);
  new_req.set_serve_as_proxy(false);

  // TODO(NIC): Change to GetCheckpointAsync like XClusterPoller::DoPoll.
  auto status = cdc_proxy->GetCheckpoint(new_req, resp, &rpc);
  RPC_STATUS_RETURN_ERROR(status, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, *context);
  context->RespondSuccess();
}

void CDCServiceImpl::GetCheckpoint(
    const GetCheckpointRequestPB* req, GetCheckpointResponsePB* resp, RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(
      req->has_tablet_id(),
      STATUS(InvalidArgument, "Tablet ID is required to get CDC checkpoint"),
      resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST,
      context);
  RPC_CHECK_AND_RETURN_ERROR(
      req->has_stream_id(),
      STATUS(InvalidArgument, "Stream ID is required to get CDC checkpoint"),
      resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST,
      context);

  auto tablet_peer = context_->LookupTablet(req->tablet_id());

  if ((!tablet_peer || tablet_peer->IsNotLeader()) && req->serve_as_proxy()) {
    // Forward GetCheckpoint() to tablet leader. This happens often in Kubernetes setups.
    return TabletLeaderGetCheckpoint(req, resp, &context);
  }

  RPC_CHECK_AND_RETURN_ERROR(
      tablet_peer && tablet_peer->IsLeaderAndReady(),
      STATUS(LeaderNotReadyToServe, "Not ready to serve"), resp->mutable_error(),
      CDCErrorPB::LEADER_NOT_READY, context);

  auto req_stream_id = RPC_VERIFY_STRING_TO_STREAM_ID(req->stream_id());
  // Check that requested tablet_id is part of the CDC stream.
  ProducerTabletInfo producer_tablet = {{}, req_stream_id, req->tablet_id()};
  auto s = CheckTabletValidForStream(producer_tablet);
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  auto stream_ptr = RPC_VERIFY_RESULT(
      GetStream(req_stream_id), resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  if (stream_ptr->GetSourceType() == CDCRequestSource::XCLUSTER) {
    auto last_checkpoint = RPC_VERIFY_RESULT(
        GetLastCheckpoint(producer_tablet, stream_ptr->GetSourceType()), resp->mutable_error(),
        CDCErrorPB::INTERNAL_ERROR, context);

    last_checkpoint.ToPB(resp->mutable_checkpoint()->mutable_op_id());
  } else {
    // CDCSDK Source type
    auto cdc_sdk_checkpoint = RPC_VERIFY_RESULT(
        GetLastCheckpointFromCdcState(req_stream_id, req->tablet_id(), stream_ptr->GetSourceType()),
        resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

    bool is_colocated = tablet_peer->tablet_metadata()->colocated() && req->has_table_id() &&
                        !req->table_id().empty();
    bool send_colocated_snapshot_checkpoint = false;
    CDCSDKCheckpointPB colocated_snapshot_checkpoint;

    if (is_colocated) {
      colocated_snapshot_checkpoint = RPC_VERIFY_RESULT(
          GetLastCheckpointFromCdcState(
              req_stream_id, req->tablet_id(), stream_ptr->GetSourceType(), req->table_id()),
          resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

      bool found_colocated_row = true;
      if (colocated_snapshot_checkpoint.term() == -1 &&
          colocated_snapshot_checkpoint.index() == -1 && !colocated_snapshot_checkpoint.has_key() &&
          !colocated_snapshot_checkpoint.has_snapshot_time()) {
        found_colocated_row = false;
        send_colocated_snapshot_checkpoint = true;
      }

      // For colocated tables, we need to see if the snapshot stage was still ongoing, in which
      // case we need to get the checkpoint details from : 'colocated_table_cdc_sdk_checkpoint'
      // i.e the specific row maintained for the colocated table.
      if (found_colocated_row && colocated_snapshot_checkpoint.has_key()) {
        send_colocated_snapshot_checkpoint = true;
      }
    }
    // 'send_colocated_snapshot_checkpoint' would only be true for colocated tablets.
    DCHECK(send_colocated_snapshot_checkpoint ? is_colocated : true);

    auto set_resp_checkpoint = [&](const CDCSDKCheckpointPB& checkpoint_pb) {
      resp->mutable_checkpoint()->mutable_op_id()->set_term(checkpoint_pb.term());
      resp->mutable_checkpoint()->mutable_op_id()->set_index(checkpoint_pb.index());
      if (checkpoint_pb.has_key()) {
        resp->set_snapshot_key(checkpoint_pb.key());
      }
      if (checkpoint_pb.has_snapshot_time()) {
        resp->set_snapshot_time(checkpoint_pb.snapshot_time());
      }
    };

    if (send_colocated_snapshot_checkpoint) {
      set_resp_checkpoint(colocated_snapshot_checkpoint);
    } else {
      set_resp_checkpoint(cdc_sdk_checkpoint);
    }
  }
  context.RespondSuccess();
}

void CDCServiceImpl::UpdateCdcReplicatedIndex(
    const UpdateCdcReplicatedIndexRequestPB* req,
    UpdateCdcReplicatedIndexResponsePB* resp,
    rpc::RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  // If we fail to update at least one tablet, roll back the replicated index for all mutated
  // tablets.
  std::vector<const std::string*> rollback_tablet_id_vec;
  RollBackTabletIdCheckpointMap rollback_tablet_id_map;
  auto scope_exit = ScopeExit([this, &rollback_tablet_id_map] {
    for (const auto& [tablet_id, rollback_checkpoint_info] : rollback_tablet_id_map) {
      VLOG(1) << "Rolling back the cdc replicated index for the tablet_id: " << tablet_id;
      RollbackCdcReplicatedIndexEntry(*tablet_id, rollback_checkpoint_info);
    }
  });

  // Backwards compatibility for deprecated fields.
  if (req->has_tablet_id() && req->has_replicated_index()) {
    Status s = UpdateCdcReplicatedIndexEntry(
        req->tablet_id(), req->replicated_index(), OpId::Max(),
        MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms)),
        &rollback_tablet_id_map, HybridTime::FromPB(req->cdc_sdk_safe_time()));
    RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);
    rollback_tablet_id_map.clear();
    context.RespondSuccess();
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(
      req->tablet_ids_size() > 0 || req->replicated_indices_size() > 0 ||
          req->replicated_terms_size() > 0,
      STATUS(
          InvalidArgument,
          "Tablet ID, Index, & Term "
          "are all required to set the log replicated index"),
      resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  RPC_CHECK_AND_RETURN_ERROR(
      req->tablet_ids_size() == req->replicated_indices_size() &&
          req->tablet_ids_size() == req->replicated_terms_size(),
      STATUS(InvalidArgument, "Tablet ID, Index, & Term Count must match"), resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST, context);

  rollback_tablet_id_vec.reserve(req->tablet_ids_size());
  for (int i = 0; i < req->tablet_ids_size(); i++) {
    const OpId& cdc_sdk_op = req->cdc_sdk_consumed_ops().empty()
                                 ? OpId::Max()
                                 : OpId::FromPB(req->cdc_sdk_consumed_ops(i));
    const MonoDelta cdc_sdk_op_id_expiration = MonoDelta::FromMilliseconds(
        req->cdc_sdk_ops_expiration_ms().empty() ? GetAtomicFlag(&FLAGS_cdc_intent_retention_ms)
                                                 : req->cdc_sdk_ops_expiration_ms(i));

    Status s = UpdateCdcReplicatedIndexEntry(
        req->tablet_ids(i), req->replicated_indices(i), cdc_sdk_op, cdc_sdk_op_id_expiration,
        &rollback_tablet_id_map,
        req->cdc_sdk_safe_times().size() > i ? HybridTime::FromPB(req->cdc_sdk_safe_times(i))
                                             : HybridTime::kInvalid);
    RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);
  }

  rollback_tablet_id_map.clear();
  context.RespondSuccess();
}

Status CDCServiceImpl::UpdateCdcReplicatedIndexEntry(
    const string& tablet_id, int64 replicated_index, const OpId& cdc_sdk_replicated_op,
    const MonoDelta& cdc_sdk_op_id_expiration,
    RollBackTabletIdCheckpointMap* rollback_tablet_id_map, const HybridTime cdc_sdk_safe_time) {
  auto tablet_peer = VERIFY_RESULT(context_->GetServingTablet(tablet_id));
  if (!tablet_peer->log_available()) {
    return STATUS(TryAgain, "Tablet peer is not ready to set its log cdc index");
  }

  if (rollback_tablet_id_map) {
    (*rollback_tablet_id_map)[&tablet_id] = {
        tablet_peer->get_cdc_min_replicated_index(), tablet_peer->cdc_sdk_min_checkpoint_op_id()};
  }

  RETURN_NOT_OK(tablet_peer->set_cdc_min_replicated_index(replicated_index));
  RETURN_NOT_OK(tablet_peer->SetCDCSDKRetainOpIdAndTime(
      cdc_sdk_replicated_op, cdc_sdk_op_id_expiration, cdc_sdk_safe_time));

  if (PREDICT_FALSE(FLAGS_TEST_cdc_inject_replication_index_update_failure)) {
    return STATUS(InternalError, "Simulated error when setting the replication index");
  }

  return Status::OK();
}

void CDCServiceImpl::RollbackCdcReplicatedIndexEntry(
    const string& tablet_id, const pair<int64_t, OpId>& rollback_checkpoint_info) {
  auto tablet_peer = context_->GetServingTablet(tablet_id);
  if (!tablet_peer.ok()) {
    LOG(WARNING) << "Unable to rollback replicated index for " << tablet_id;
    return;
  }

  WARN_NOT_OK(
      (**tablet_peer).set_cdc_min_replicated_index(rollback_checkpoint_info.first),
      "Unable to update min index for tablet $0 " + tablet_id);
  WARN_NOT_OK(
      (**tablet_peer)
          .SetCDCSDKRetainOpIdAndTime(
              rollback_checkpoint_info.second,
              MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms)),
              HybridTime::kInvalid),
      "Unable to update op id and expiration time for tablet $0 " + tablet_id);
}

// Given a list of tablet ids, retrieve the latest entry op_id for each of them.
// The response should contain a list of op_ids for each input tablet id that was
// successfully processed, in the same order that the tablet ids were passed in.
Result<GetLatestEntryOpIdResponsePB> CDCServiceImpl::GetLatestEntryOpId(
    const GetLatestEntryOpIdRequestPB& req, CoarseTimePoint deadline) {
  GetLatestEntryOpIdResponsePB resp;

  std::vector<TabletId> tablet_ids;
  if (req.has_tablet_id()) {
    // Support backwards compatibility.
    tablet_ids.push_back(req.tablet_id());
  } else {
    for (int i = 0; i < req.tablet_ids_size(); i++) {
      tablet_ids.push_back(req.tablet_ids(i));
    }
  }

  if (tablet_ids.empty()) {
    return STATUS(
        InvalidArgument, "Tablet IDs are required to set the log replicated index",
        CDCError(CDCErrorPB::INVALID_REQUEST));
  }

  HybridTime bootstrap_time = HybridTime::kMin;
  for (auto& tablet_id : tablet_ids) {
    auto tablet_peer = VERIFY_RESULT_OR_SET_CODE(
        context_->GetServingTablet(tablet_id), CDCError(CDCErrorPB::INTERNAL_ERROR));

    if (!tablet_peer->log_available()) {
      const string err_message = strings::Substitute(
          "Unable to get the latest entry op id from "
          "peer $0 and tablet $1 because its log object hasn't been initialized",
          tablet_peer->permanent_uuid(), tablet_peer->tablet_id());
      LOG(WARNING) << err_message;
      return STATUS(ServiceUnavailable, err_message, CDCError(CDCErrorPB::INTERNAL_ERROR));
    }

    // Add op_id to response.
    auto [op_id, ht] = VERIFY_RESULT(tablet_peer->GetOpIdAndSafeTimeForXReplBootstrap());
    op_id.ToPB(resp.add_op_ids());
    bootstrap_time.MakeAtLeast(ht);
  }

  if (!bootstrap_time.is_special()) {
    resp.set_bootstrap_time(bootstrap_time.ToUint64());
  }

  return resp;
}

void CDCServiceImpl::GetCDCDBStreamInfo(
    const GetCDCDBStreamInfoRequestPB* req,
    GetCDCDBStreamInfoResponsePB* resp,
    rpc::RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  LOG(INFO) << "Received GetCDCDBStreamInfo request " << req->ShortDebugString();

  RPC_CHECK_AND_RETURN_ERROR(
      req->has_db_stream_id(),
      STATUS(InvalidArgument, "Database Stream ID is required to get DB stream information"),
      resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST,
      context);

  std::vector<pair<std::string, std::string>> db_stream_info;
  Status s = client()->GetCDCDBStreamInfo(req->db_stream_id(), &db_stream_info);
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  for (const auto& tabinfo : db_stream_info) {
    auto* const table_info = resp->add_table_info();
    table_info->set_stream_id(tabinfo.first);
    table_info->set_table_id(tabinfo.second);
  }

  context.RespondSuccess();
}

void CDCServiceImpl::RollbackPartialCreate(const CDCCreationState& creation_state) {
  const TabletCDCCheckpointInfo kOpIdMax;

  if (!creation_state.created_cdc_streams.empty()) {
    WARN_NOT_OK(
        client()->DeleteCDCStream(creation_state.created_cdc_streams),
        Format("Unable to delete streams $0", yb::ToString(creation_state.created_cdc_streams)));
  }

  // For all tablets we modified state for, reverse those changes if the operation failed
  // halfway through.
  if (creation_state.producer_entries_modified.empty()) {
    return;
  }
  {
    std::lock_guard l(mutex_);
    // Erase tablet_checkpoint_ entries.
    // TODO: Also need to clear tablet_checkpoints_ on remote peers as well, since currently they
    // would never get cleared until a node restart.
    impl_->EraseStreams(creation_state.producer_entries_modified, false);
  }

  // TODO(1:N): Need to support rollback if we already have an existing stream set up (GH #18817).
  // Currently, we always reset cdc_min_replicated_index to OpId::Max (should be previous value).
  // TODO: Do we need to clean this up - can we just use reset_cdc_min_replicated_index_if_stale?
  for (const auto& entry : creation_state.producer_entries_modified) {
    // Update the term and index for the consumed checkpoint to tablet's LEADER as well as FOLLOWER.
    auto tablet_peer = context_->GetServingTablet(entry.tablet_id);
    if (tablet_peer.ok()) {  // if local
      WARN_NOT_OK(
          (**tablet_peer).set_cdc_min_replicated_index(kOpIdMax.cdc_op_id.index),
          "Unable to update min index for local tablet " + entry.tablet_id);
    }
    WARN_NOT_OK(
        UpdatePeersCdcMinReplicatedIndex(entry.tablet_id, kOpIdMax),
        "Unable to update min index for remote tablet " + entry.tablet_id);
  }
}

void CDCServiceImpl::BootstrapProducer(
    const BootstrapProducerRequestPB* req, BootstrapProducerResponsePB* resp,
    rpc::RpcContext context) {
  LOG(INFO) << "Received BootstrapProducer request " << req->ShortDebugString();
  RPC_CHECK_AND_RETURN_ERROR(
      req->table_ids().size() > 0,
      STATUS(InvalidArgument, "Table ID is required to create CDC stream"), resp->mutable_error(),
      CDCErrorPB::INVALID_REQUEST, context);

  // Used to delete streams in case of failure.
  CDCCreationState creation_state;
  auto scope_exit = ScopeExit([this, &creation_state] { RollbackPartialCreate(creation_state); });

  XClusterProducerBootstrap xcluster_producer_bootstrap(
      this, *req, resp, &creation_state, context_.get(), cdc_state_table_.get());
  Status s = xcluster_producer_bootstrap.RunBootstrapProducer();
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  // Clear these vectors so no changes are reversed by scope_exit since we succeeded.
  creation_state.Clear();
  context.RespondSuccess();
}

void CDCServiceImpl::Shutdown() {
  if (impl_->async_client_init_) {
    impl_->async_client_init_->Shutdown();
    rpcs_.Shutdown();
    {
      std::lock_guard l(mutex_);
      cdc_service_stopped_ = true;
    }
    if (update_peers_and_metrics_thread_) {
      update_peers_and_metrics_thread_->Join();
    }

    cdc_state_table_.reset();
    impl_->async_client_init_ = nullptr;
    impl_->ClearCaches();
  }
}

Status CDCServiceImpl::CheckStreamActive(
    const ProducerTabletInfo& producer_tablet, const int64_t& last_active_time_passed) {
  auto last_active_time = (last_active_time_passed == 0)
                              ? VERIFY_RESULT(GetLastActiveTime(producer_tablet))
                              : last_active_time_passed;

  auto now = GetCurrentTimeMicros();
  if (now < last_active_time + 1000 * (GetAtomicFlag(&FLAGS_cdc_intent_retention_ms))) {
    VLOG(1) << "Tablet: " << producer_tablet.ToString()
            << " found in CDCState table/ cache with active time: " << last_active_time
            << " current time:" << now << ", for stream: " << producer_tablet.stream_id;
    return Status::OK();
  }

  last_active_time = VERIFY_RESULT(GetLastActiveTime(producer_tablet, true));
  if (now < last_active_time + 1000 * (GetAtomicFlag(&FLAGS_cdc_intent_retention_ms))) {
    VLOG(1) << "Tablet: " << producer_tablet.ToString()
            << " found in CDCState table with active time: " << last_active_time
            << " current time:" << now << ", for stream: " << producer_tablet.stream_id;
    return Status::OK();
  }

  VLOG(1) << "Stream: " << producer_tablet.stream_id
          << ", is expired for tablet: " << producer_tablet.tablet_id
          << ", active time in CDCState table: " << last_active_time << ", current time: " << now;
  return STATUS_FORMAT(
      InternalError, "Stream ID $0 is expired for Tablet ID $1", producer_tablet.stream_id,
      producer_tablet.tablet_id);
}

Result<int64_t> CDCServiceImpl::GetLastActiveTime(
    const ProducerTabletInfo& producer_tablet, bool ignore_cache) {
  DCHECK(producer_tablet.stream_id && !producer_tablet.tablet_id.empty());

  if (!ignore_cache) {
    auto result = impl_->GetLastActiveTime(producer_tablet);
    if (result) {
      return *result;
    }
  }

  auto entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
      {producer_tablet.tablet_id, producer_tablet.stream_id},
      CDCStateTableEntrySelector().IncludeData()));
  if (!entry_opt) {
    // This could happen when concurrently as this function is running the stream is deleted, in
    // which case we return last active_time as "0".
    return 0;
  }

  if (entry_opt->active_time) {
    auto last_active_time = *entry_opt->active_time;

    VLOG(2) << "Found entry in cdc_state table with active time: " << last_active_time
            << ", for tablet: " << producer_tablet.tablet_id
            << ", and stream: " << producer_tablet.stream_id;
    return last_active_time;
  }

  return GetCurrentTimeMicros();
}

Result<CDCSDKCheckpointPB> CDCServiceImpl::GetLastCheckpointFromCdcState(
    const xrepl::StreamId& stream_id, const TabletId& tablet_id,
    const CDCRequestSource& request_source, const TableId& colocated_table_id,
    const bool ignore_unpolled_tablets) {
  auto entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
      {tablet_id, stream_id, colocated_table_id},
      CDCStateTableEntrySelector().IncludeCheckpoint().IncludeLastReplicationTime().IncludeData()));

  CDCSDKCheckpointPB cdc_sdk_checkpoint_pb;
  if (!entry_opt) {
    LOG(WARNING) << "Did not find any row in the cdc state table for tablet: " << tablet_id
                 << ", stream: " << stream_id << ", colocated_table_id:" << colocated_table_id;
    if (colocated_table_id.empty()) {
      cdc_sdk_checkpoint_pb.set_term(0);
      cdc_sdk_checkpoint_pb.set_index(0);
    } else {
      // In cases of colocated_table_id is true, we need to return OpId::Invalid(), to indicate no
      // row was found.
      cdc_sdk_checkpoint_pb.set_term(-1);
      cdc_sdk_checkpoint_pb.set_index(-1);
    }
    return cdc_sdk_checkpoint_pb;
  }

  const auto& entry = *entry_opt;
  auto& cdc_sdk_op_id = *entry.checkpoint;

  if (ignore_unpolled_tablets && !entry.last_replication_time
        && request_source == CDCRequestSource::CDCSDK) {
    // This would mean the row is un-polled through GetChanges, since the 'kCdcLastReplicationTime'
    // column is null. There is a small window where children tablets after tablet split have a
    // valid checkpoint but they will not have the 'kCdcLastReplicationTime' value set.
    cdc_sdk_checkpoint_pb.set_term(-1);
    cdc_sdk_checkpoint_pb.set_index(-1);
    return cdc_sdk_checkpoint_pb;
  }

  if (entry.cdc_sdk_safe_time) {
    cdc_sdk_checkpoint_pb.set_snapshot_time(*entry.cdc_sdk_safe_time);
  } else {
    cdc_sdk_checkpoint_pb.set_snapshot_time(0);
  }

  // If we do not find the 'kCDCSDKSnapshotKey' key in the 'kCdcData' column, we will infer that
  // the snapshot is completed, and hence we will not populate the snapshot key.
  if (entry.snapshot_key) {
    cdc_sdk_checkpoint_pb.set_key(*entry.snapshot_key);
  }

  cdc_sdk_checkpoint_pb.set_term(cdc_sdk_op_id.term);
  cdc_sdk_checkpoint_pb.set_index(cdc_sdk_op_id.index);

  return cdc_sdk_checkpoint_pb;
}

Result<OpId> CDCServiceImpl::GetLastCheckpoint(
    const ProducerTabletInfo& producer_tablet, const CDCRequestSource& request_source,
    const bool ignore_unpolled_tablets) {
  if (!PREDICT_FALSE(FLAGS_TEST_force_get_checkpoint_from_cdc_state)) {
    auto result = impl_->GetLastCheckpoint(producer_tablet);
    if (result) {
      return *result;
    }
  }

  const auto cdc_sdk_checkpoint = VERIFY_RESULT(GetLastCheckpointFromCdcState(
      producer_tablet.stream_id, producer_tablet.tablet_id, request_source,
      "", ignore_unpolled_tablets /* ignore_unpolled_tablets */));
  return OpId(cdc_sdk_checkpoint.term(), cdc_sdk_checkpoint.index());
}

Result<uint64_t> CDCServiceImpl::GetSafeTime(
    const xrepl::StreamId& stream_id, const TabletId& tablet_id) {
  const auto cdc_sdk_checkpoint =
      VERIFY_RESULT(GetLastCheckpointFromCdcState(stream_id, tablet_id, CDCRequestSource::CDCSDK));

  return cdc_sdk_checkpoint.has_snapshot_time() ? cdc_sdk_checkpoint.snapshot_time() : 0;
}

bool RecordHasValidOp(const CDCSDKProtoRecordPB& record) {
  return record.row_message().op() == RowMessage_Op_INSERT ||
         record.row_message().op() == RowMessage_Op_UPDATE ||
         record.row_message().op() == RowMessage_Op_DELETE ||
         record.row_message().op() == RowMessage_Op_SAFEPOINT ||
         record.row_message().op() == RowMessage_Op_READ;
}

// Find the right-most proto record from the cdc_sdk_proto_records
// having valid commit_time, which will be used to calculate
// CDCSDK lag metrics cdcsdk_sent_lag_micros.
boost::optional<MicrosTime> GetCDCSDKLastSendRecordTime(const GetChangesResponsePB* resp) {
  int cur_idx = resp->cdc_sdk_proto_records_size() - 1;
  while (cur_idx >= 0) {
    auto& each_record = resp->cdc_sdk_proto_records(cur_idx);
    if (RecordHasValidOp(each_record)) {
      return HybridTime(each_record.row_message().commit_time()).GetPhysicalValueMicros();
    }
    cur_idx -= 1;
  }
  return boost::optional<MicrosTime>{};
}

void CDCServiceImpl::UpdateCDCTabletMetrics(
    const GetChangesResponsePB* resp,
    const ProducerTabletInfo& producer_tablet,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const OpId& op_id,
    const CDCRequestSource source_type,
    int64_t last_readable_index) {
  auto tablet_metric_row = GetCDCTabletMetrics(producer_tablet, tablet_peer, source_type);
  if (!tablet_metric_row) {
    return;
  }

  if (source_type == CDCSDK) {
    auto tablet_metric = std::static_pointer_cast<CDCSDKTabletMetrics>(tablet_metric_row);
    tablet_metric->cdcsdk_change_event_count->IncrementBy(resp->cdc_sdk_proto_records_size());
    tablet_metric->cdcsdk_expiry_time_ms->set_value(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms));
    if (resp->cdc_sdk_proto_records_size() > 0) {
      tablet_metric->cdcsdk_traffic_sent->IncrementBy(
          resp->cdc_sdk_proto_records_size() * resp->cdc_sdk_proto_records(0).ByteSize());
      auto last_record_time = GetCDCSDKLastSendRecordTime(resp);
      auto last_record_micros =
          last_record_time
              ? last_record_time.get()
              : tablet_metric->cdcsdk_last_sent_physicaltime->value();
      auto last_replicated_micros = GetLastReplicatedTime(tablet_peer);
      tablet_metric->cdcsdk_last_sent_physicaltime->set_value(last_record_micros);
      tablet_metric->cdcsdk_sent_lag_micros->set_value(last_replicated_micros - last_record_micros);

    } else {
      auto last_replicated_micros = GetLastReplicatedTime(tablet_peer);
      tablet_metric->cdcsdk_last_sent_physicaltime->set_value(last_replicated_micros);
      tablet_metric->cdcsdk_sent_lag_micros->set_value(0);
    }

  } else {
    auto tablet_metric = std::static_pointer_cast<CDCTabletMetrics>(tablet_metric_row);
    auto lid = resp->checkpoint().op_id();
    tablet_metric->last_read_opid_term->set_value(lid.term());
    tablet_metric->last_read_opid_index->set_value(lid.index());
    tablet_metric->last_readable_opid_index->set_value(last_readable_index);
    tablet_metric->last_checkpoint_opid_index->set_value(op_id.index);
    tablet_metric->last_getchanges_time->set_value(GetCurrentTimeMicros());

    if (resp->records_size() > 0) {
      uint64 last_record_time = resp->records(resp->records_size() - 1).time();
      uint64 first_record_time = resp->records(0).time();

      tablet_metric->last_read_hybridtime->set_value(last_record_time);
      auto last_record_micros = HybridTime(last_record_time).GetPhysicalValueMicros();
      tablet_metric->last_read_physicaltime->set_value(last_record_micros);
      // Only count bytes responded if we are including a response payload.
      tablet_metric->rpc_payload_bytes_responded->Increment(resp->ByteSize());
      // Get the physical time of the last committed record on producer.
      auto last_replicated_micros = GetLastReplicatedTime(tablet_peer);
      tablet_metric->async_replication_sent_lag_micros->set_value(
          last_replicated_micros - last_record_micros);

      auto first_record_micros = HybridTime(first_record_time).GetPhysicalValueMicros();
      tablet_metric->last_checkpoint_physicaltime->set_value(first_record_micros);
      // When there is lag between consumer and producer, consumer is caught up to either
      // the previous caught-up time, or to the last committed record time on consumer.
      tablet_metric->last_caughtup_physicaltime->set_value(
          std::max(tablet_metric->last_caughtup_physicaltime->value(), first_record_micros));
      tablet_metric->async_replication_committed_lag_micros->set_value(
          last_replicated_micros - first_record_micros);
    } else {
      tablet_metric->rpc_heartbeats_responded->Increment();
      // If there are no more entries to be read, that means we're caught up.
      auto last_replicated_micros = GetLastReplicatedTime(tablet_peer);
      tablet_metric->last_read_physicaltime->set_value(last_replicated_micros);
      tablet_metric->last_checkpoint_physicaltime->set_value(last_replicated_micros);
      tablet_metric->last_caughtup_physicaltime->set_value(GetCurrentTimeMicros());
      tablet_metric->async_replication_sent_lag_micros->set_value(0);
      tablet_metric->async_replication_committed_lag_micros->set_value(0);
    }
  }
}

bool CDCServiceImpl::IsCDCSDKSnapshotDone(const GetChangesRequestPB& req) {
  if (req.from_cdc_sdk_checkpoint().has_write_id() &&
      req.from_cdc_sdk_checkpoint().write_id() == 0 &&
      req.from_cdc_sdk_checkpoint().key() == kCDCSDKSnapshotDoneKey &&
      req.from_cdc_sdk_checkpoint().snapshot_time() == 0) {
    return true;
  }

  return false;
}

bool CDCServiceImpl::IsCDCSDKSnapshotRequest(const CDCSDKCheckpointPB& req_checkpoint) {
  return req_checkpoint.write_id() == -1;
}

bool CDCServiceImpl::IsCDCSDKSnapshotBootstrapRequest(const CDCSDKCheckpointPB& req_checkpoint) {
  return req_checkpoint.write_id() == -1 && req_checkpoint.key().empty();
}

Status CDCServiceImpl::InsertRowForColocatedTableInCDCStateTable(
    const ProducerTabletInfo& producer_tablet,
    const TableId& colocated_table_id,
    const OpId& commit_op_id,
    const HybridTime& cdc_sdk_safe_time) {
  DCHECK(
      producer_tablet.stream_id && !producer_tablet.tablet_id.empty() &&
      !colocated_table_id.empty());

  // We will store a string of the format: '<stream_id>_<table_id>' in the cdc_state table under the
  // stream_id column.
  CDCStateTableEntry entry(
      producer_tablet.tablet_id,
      producer_tablet.stream_id, colocated_table_id);
  entry.checkpoint = commit_op_id;
  entry.last_replication_time = 0;
  entry.cdc_sdk_safe_time = cdc_sdk_safe_time.ToUint64();
  entry.snapshot_key = "";

  return cdc_state_table_->InsertEntries({entry});
}

Status CDCServiceImpl::UpdateCheckpointAndActiveTime(
    const ProducerTabletInfo& producer_tablet,
    const OpId& sent_op_id,
    const OpId& commit_op_id,
    uint64_t last_record_hybrid_time,
    const CDCRequestSource& request_source,
    const bool snapshot_bootstrap,
    const HybridTime& cdc_sdk_safe_time,
    const bool is_snapshot,
    const std::string& snapshot_key,
    const TableId& colocated_table_id) {
  bool update_cdc_state = impl_->UpdateCheckpoint(producer_tablet, sent_op_id, commit_op_id);
  if (!update_cdc_state && !snapshot_bootstrap) {
    return Status::OK();
  }

  bool update_colocated_snapshot_row =
      is_snapshot && !colocated_table_id.empty() && request_source == CDCSDK;
  // In case of updating the checkpoint during snapshot process of a colocated table, we will first
  // need to update the checkpoint for the colocated table (i.e kCdcStreamId column has
  // streamId_TableId), and then update the active time of the tablet row. In all other cases this
  // we will only have to update a single row.
  DCHECK(producer_tablet.stream_id && !producer_tablet.tablet_id.empty());
  const auto& last_active_time = GetCurrentTimeMicros();

  {
    CDCStateTableKey key(producer_tablet.tablet_id, producer_tablet.stream_id);
    if (update_colocated_snapshot_row) {
      key = CDCStateTableKey(
          producer_tablet.tablet_id, producer_tablet.stream_id, colocated_table_id);
    }
    CDCStateTableEntry entry(std::move(key));
    entry.checkpoint = commit_op_id;
    // If we have a last record hybrid time, use that for physical time. If not, it means we're
    // caught up, so the current time.
    entry.last_replication_time = last_record_hybrid_time != 0
                                      ? HybridTime(last_record_hybrid_time).GetPhysicalValueMicros()
                                      : GetCurrentTimeMicros();

    if (request_source == CDCSDK) {
      entry.active_time = last_active_time;
      entry.cdc_sdk_safe_time = cdc_sdk_safe_time.ToUint64();

      if (is_snapshot) {
        // The 'GetChanges' call bootstrapping snapshot will have snapshot key empty.
        // In cases of taking snapshot for a colocated table, we will only update the "snapshot_key"
        // in the rows for meant each colocated tableId.
        entry.snapshot_key = snapshot_key;
      }

      VLOG(2) << "Updating cdc state table with: checkpoint: " << commit_op_id.ToString()
              << ", last active time: " << last_active_time << ", safe time: " << cdc_sdk_safe_time
              << ", for tablet: " << producer_tablet.tablet_id
              << ", and stream: " << producer_tablet.stream_id;
    }

    RETURN_NOT_OK(cdc_state_table_->UpdateEntries({entry}));
  }

  // If we update the colocated snapshot row, we still need to do one of two things:
  // 1. Update the "safe_time" in the row used for streaming, if the checkpoint is not set
  // 2. Else, Only update the active time on the row used for streaming
  if (update_colocated_snapshot_row) {
    auto checkpoint = VERIFY_RESULT(GetLastCheckpointFromCdcState(
        producer_tablet.stream_id, producer_tablet.tablet_id, CDCRequestSource::CDCSDK));

    if (snapshot_bootstrap && checkpoint.term() == 0 && checkpoint.index() == 0) {
      RETURN_NOT_OK(UpdateCheckpointAndActiveTime(
          producer_tablet, sent_op_id, commit_op_id, last_record_hybrid_time, request_source,
          snapshot_bootstrap, cdc_sdk_safe_time, is_snapshot, "", ""));
    } else {
      CDCStateTableEntry entry(producer_tablet.tablet_id, producer_tablet.stream_id);
      entry.active_time = last_active_time;
      entry.cdc_sdk_safe_time = checkpoint.has_snapshot_time() ? checkpoint.snapshot_time() : 0;
      RETURN_NOT_OK(cdc_state_table_->UpdateEntries({entry}));
    }
  }

  return Status::OK();
}

Status CDCServiceImpl::UpdateSnapshotDone(
    const xrepl::StreamId& stream_id,
    const TabletId& tablet_id,
    const TableId& colocated_table_id,
    const CDCSDKCheckpointPB& cdc_sdk_checkpoint) {
  DCHECK(stream_id && !tablet_id.empty());

  auto current_time = GetCurrentTimeMicros();
  {
    CDCStateTableEntry entry(tablet_id, stream_id, colocated_table_id);
    entry.cdc_sdk_safe_time =
        !cdc_sdk_checkpoint.has_snapshot_time() ? 0 : cdc_sdk_checkpoint.snapshot_time();
    entry.active_time = current_time;
    entry.last_replication_time = 0;
    // Insert if row not found, Update if row already exists.
    RETURN_NOT_OK(cdc_state_table_->UpsertEntries({entry}));
  }

  // Also update the active_time in the streaming row.
  if (!colocated_table_id.empty()) {
    CDCStateTableEntry entry(tablet_id, stream_id);
    entry.active_time = current_time;
    entry.cdc_sdk_safe_time = VERIFY_RESULT(GetSafeTime(stream_id, tablet_id));
    RETURN_NOT_OK(cdc_state_table_->UpdateEntries({entry}));
  }

  return Status::OK();
}

const std::string GetCDCMetricsKey(const xrepl::StreamId& stream_id) {
  return "CDCMetrics::" + stream_id.ToString();
}

std::shared_ptr<void> CDCServiceImpl::GetCDCTabletMetrics(
    const ProducerTabletInfo& producer,
    std::shared_ptr<tablet::TabletPeer>
        tablet_peer,
    CDCRequestSource source_type,
    CreateCDCMetricsEntity create) {
  // 'nullptr' not recommended: using for tests.
  if (tablet_peer == nullptr) {
    auto tablet_peer_result = context_->GetServingTablet(producer.tablet_id);
    if (!tablet_peer_result.ok()) return nullptr;
    tablet_peer = std::move(*tablet_peer_result);
  }

  auto tablet = tablet_peer->shared_tablet();
  if (tablet == nullptr) return nullptr;

  const std::string key = GetCDCMetricsKey(producer.stream_id);
  std::shared_ptr<void> metrics_raw = tablet->GetAdditionalMetadata(key);
  if (metrics_raw == nullptr && create) {
    //  Create a new METRIC_ENTITY_cdc here.
    MetricEntity::AttributeMap attrs;
    {
      SharedLock<rw_spinlock> l(mutex_);
      auto raft_group_metadata = tablet->metadata();
      attrs["table_id"] = raft_group_metadata->table_id();
      attrs["namespace_name"] = raft_group_metadata->namespace_name();
      attrs["table_name"] = raft_group_metadata->table_name();
      attrs["table_type"] = TableType_Name(raft_group_metadata->table_type());
      attrs["stream_id"] = producer.stream_id.ToString();
    }

    scoped_refptr<yb::MetricEntity> entity;
    if (source_type == CDCSDK) {
      entity = METRIC_ENTITY_cdcsdk.Instantiate(metric_registry_, producer.MetricsString(), attrs);
      metrics_raw = std::make_shared<CDCSDKTabletMetrics>(entity);

    } else {
      entity = METRIC_ENTITY_cdc.Instantiate(metric_registry_, producer.MetricsString(), attrs);
      metrics_raw = std::make_shared<CDCTabletMetrics>(entity);
    }
    // Adding the new metric to the tablet so it maintains the same lifetime scope.
    tablet->AddAdditionalMetadata(key, metrics_raw);
  }
  return metrics_raw;
}

void CDCServiceImpl::RemoveCDCTabletMetrics(
    const ProducerTabletInfo& producer, std::shared_ptr<tablet::TabletPeer> tablet_peer) {
  if (tablet_peer == nullptr) {
    LOG(WARNING) << "Received null tablet peer pointer.";
    return;
  }
  auto tablet = tablet_peer->shared_tablet();
  if (tablet == nullptr) {
    LOG(WARNING) << "Could not find tablet for tablet peer: " << tablet_peer->tablet_id();
    return;
  }

  const std::string key = GetCDCMetricsKey(producer.stream_id);

  // Removing the metadata still leaves the metric_entity in metric_registry_, but that will be
  // cleaned up as part of RetireOldMetrics(), as the metric_registry_ will be the only ref left.
  tablet->RemoveAdditionalMetadata(key);
}

Result<std::shared_ptr<StreamMetadata>> CDCServiceImpl::GetStream(
    const xrepl::StreamId& stream_id, RefreshStreamMapOption opts) {
  std::shared_ptr<StreamMetadata> stream_metadata;
  {
    SharedLock l(mutex_);
    stream_metadata = FindPtrOrNull(stream_metadata_, stream_id);
  }

  if (stream_metadata == nullptr) {
    std::lock_guard l(mutex_);
    stream_metadata =
        LookupOrInsert(&stream_metadata_, stream_id, std::make_shared<StreamMetadata>());
  }

  RETURN_NOT_OK(stream_metadata->InitOrReloadIfNeeded(stream_id, opts, client()));

  return stream_metadata;
}

std::vector<xrepl::StreamTabletStats> CDCServiceImpl::GetAllStreamTabletStats() const {
  std::vector<xrepl::StreamTabletStats> result;
  SharedLock l(mutex_);
  for (const auto& [stream_id, metadata] : stream_metadata_) {
    auto tablet_status = metadata->GetAllStreamTabletStats(stream_id);
    result.insert(
        std::end(result), std::make_move_iterator(std::begin(tablet_status)),
        std::make_move_iterator(std::end(tablet_status)));
  }
  return result;
}

void CDCServiceImpl::RemoveStreamFromCache(const xrepl::StreamId& stream_id) {
  std::lock_guard l(mutex_);
  stream_metadata_.erase(stream_id);
}

void CDCServiceImpl::AddStreamMetadataToCache(
    const xrepl::StreamId& stream_id, const std::shared_ptr<StreamMetadata>& stream_metadata) {
  std::lock_guard l(mutex_);
  InsertOrUpdate(&stream_metadata_, stream_id, stream_metadata);
}

Status CDCServiceImpl::CheckTabletValidForStream(const ProducerTabletInfo& info) {
  auto result = VERIFY_RESULT(impl_->PreCheckTabletValidForStream(info));
  if (result) {
    return Status::OK();
  }
  // If we don't recognize the tablet_id, populate our full tablet list for this stream.
  // This can happen if we call "GetChanges" on a split tablet. We will initalise the entries for
  // the split tablets in both: tablet_checkpoints_ and cdc_state_metadata_.
  auto tablets = VERIFY_RESULT(GetTablets(info.stream_id, true /* ignore_errors */));

  auto status = impl_->CheckTabletValidForStream(info, tablets);

  if (status.IsInvalidArgument()) {
    // We check and see if tablet split has occured on the tablet.
    for (const auto& tablet : tablets) {
      if (tablet.has_split_parent_tablet_id() &&
          tablet.split_parent_tablet_id() == info.tablet_id) {
        return STATUS_FORMAT(
            TabletSplit, Format("Tablet Split detected on $0 : $1", info.tablet_id, status));
      }
    }
  }

  return status;
}

void CDCServiceImpl::IsBootstrapRequired(
    const IsBootstrapRequiredRequestPB* req,
    IsBootstrapRequiredResponsePB* resp,
    rpc::RpcContext context) {
  RPC_CHECK_AND_RETURN_ERROR(
      req->tablet_ids_size() > 0,
      STATUS(InvalidArgument, "Tablet ID is required to check for replication"),
      resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  for (auto& tablet_id : req->tablet_ids()) {
    auto tablet_peer = context_->LookupTablet(tablet_id);

    RPC_CHECK_AND_RETURN_ERROR(
        tablet_peer && tablet_peer->IsLeaderAndReady(),
        STATUS(LeaderNotReadyToServe, "Not ready to serve"), resp->mutable_error(),
        CDCErrorPB::LEADER_NOT_READY, context);
    std::shared_ptr<CDCTabletMetrics> tablet_metric = NULL;

    OpId op_id = OpId::Invalid();
    if (req->has_stream_id() && !req->stream_id().empty()) {
      auto stream_id = RPC_VERIFY_STRING_TO_STREAM_ID(req->stream_id());
      // Check that requested tablet_id is part of the CDC stream.
      ProducerTabletInfo producer_tablet = {{}, stream_id, tablet_id};
      RPC_STATUS_RETURN_ERROR(
          CheckTabletValidForStream(producer_tablet), resp->mutable_error(),
          CDCErrorPB::INVALID_REQUEST, context);

      op_id = RPC_VERIFY_RESULT(
          GetLastCheckpoint(producer_tablet, CDCRequestSource::XCLUSTER), resp->mutable_error(),
          CDCErrorPB::INTERNAL_ERROR, context);

      tablet_metric = std::static_pointer_cast<CDCTabletMetrics>(
          GetCDCTabletMetrics({{}, stream_id, tablet_id}, tablet_peer));
    }

    auto is_bootstrap_required = RPC_VERIFY_RESULT(
        IsBootstrapRequiredForTablet(tablet_peer, op_id, context.GetClientDeadline()),
        resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

    if (is_bootstrap_required) {
      resp->set_bootstrap_required(true);

      if (!tablet_metric) {
        // If we don't have a metrics, return immediately.
        break;
      }
    }

    if (tablet_metric) {
      // TODO: Computing this on producer side is expensive. Replace this producer side metric with
      // consumer side APIs or metrics.
      tablet_metric->is_bootstrap_required->set_value(is_bootstrap_required ? 1 : 0);
    }
  }
  context.RespondSuccess();
}

Status CDCServiceImpl::UpdateChildrenTabletsOnSplitOpForCDCSDK(const ProducerTabletInfo& info) {
  auto tablets = VERIFY_RESULT(GetTablets(info.stream_id, true /* ignore_errors */));

  // Initializing the children to 0.0 to prevent garbage collection on them.
  const OpId& children_op_id = OpId();

  std::array<TabletId, 2> children;
  uint found_children = 0;
  for (auto const& tablet : tablets) {
    if (tablet.has_split_parent_tablet_id() && tablet.split_parent_tablet_id() == info.tablet_id) {
      children[found_children] = tablet.tablet_id();
      found_children += 1;

      if (found_children == 2) {
        break;
      }
    }
  }

  RSTATUS_DCHECK(
      found_children == 2, InternalError,
      Format("Could not find the two split children for tablet: $0", info.tablet_id));

  RETURN_NOT_OK_SET_CODE(
      impl_->AddEntriesForChildrenTabletsOnSplitOp(info, children, children_op_id),
      CDCError(CDCErrorPB::INTERNAL_ERROR));
  VLOG(1) << "Added entries for children tablets: " << children[0] << " and "
          << children[1] << ", of parent tablet: " << info.tablet_id
          << ", to 'cdc_state_metadata_' and 'tablet_checkpoints_'";

  return Status::OK();
}

Status CDCServiceImpl::UpdateChildrenTabletsOnSplitOpForXCluster(
    const ProducerTabletInfo& producer_tablet, const consensus::ReplicateMsg& split_op_msg) {
  const auto& split_req = split_op_msg.split_request();
  const auto parent_tablet = split_req.tablet_id();
  const vector<string> children_tablets = {split_req.new_tablet1_id(), split_req.new_tablet2_id()};

  // First check if the children tablet entries exist yet in cdc_state.
  for (const auto& child_tablet_id : children_tablets) {
    auto entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
        {child_tablet_id, producer_tablet.stream_id},
        CDCStateTableEntrySelector().IncludeCheckpoint()));
    if (!entry_opt) {
      return STATUS_FORMAT(
          NotFound,
          "Did not find a row in the cdc_state table for parent tablet: $0, child tablet: $1 and "
          "stream: $2",
          producer_tablet.tablet_id, child_tablet_id, producer_tablet.stream_id);
    }
  }

  // Force an update of parent tablet checkpoint/timestamp to ensure that there it gets updated at
  // least once (otherwise, we may have a situation where consecutive splits occur within the
  // cdc_state table update window, and we wouldn't update the tablet's row with non-null values).
  impl_->ForceCdcStateUpdate(producer_tablet);

  std::vector<CDCStateTableEntry> entries;
  // If we found both entries then lets update their checkpoints to this split_op's op id, to
  // ensure that we continue replicating from where we left off.
  for (const auto& child_tablet : children_tablets) {
    CDCStateTableEntry entry(child_tablet, producer_tablet.stream_id);
    // No need to update the timestamp here as we haven't started replicating the child yet.
    entry.checkpoint = OpId(split_op_msg.id().term(), split_op_msg.id().index());
    LOG(INFO) << "Updating cdc_state entry for split child tablet: " << entry.ToString();
    entries.push_back(std::move(entry));
  }
  return cdc_state_table_->UpdateEntries(entries);
}

void CDCServiceImpl::CheckReplicationDrain(
    const CheckReplicationDrainRequestPB* req,
    CheckReplicationDrainResponsePB* resp,
    rpc::RpcContext context) {
  RPC_CHECK_AND_RETURN_ERROR(
      req->stream_info_size() > 0,
      STATUS(
          InvalidArgument,
          "At least one (stream ID, tablet ID) pair required to check "
          "for replication drain"),
      resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);
  RPC_CHECK_AND_RETURN_ERROR(
      req->has_target_time(),
      STATUS(InvalidArgument, "target_time is required to check for replication drain"),
      resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  std::vector<std::pair<xrepl::StreamId, TabletId>> stream_tablet_to_check;
  stream_tablet_to_check.reserve(req->stream_info_size());
  for (const auto& stream_info : req->stream_info()) {
    auto stream_id = RPC_VERIFY_STRING_TO_STREAM_ID(stream_info.stream_id());
    stream_tablet_to_check.push_back({stream_id, stream_info.tablet_id()});
  }

  // Rate limiting.
  int num_retry = 0;
  auto sleep_while_unfinished = [&]() {
    if ((++num_retry) >= GetAtomicFlag(&FLAGS_wait_replication_drain_tserver_max_retry) ||
        stream_tablet_to_check.empty()) {
      return false;
    }
    SleepFor(MonoDelta::FromMilliseconds(
        GetAtomicFlag(&FLAGS_wait_replication_drain_tserver_retry_interval_ms)));
    return true;
  };

  do {
    // (stream ID, tablet ID) pairs to keep checking in the next iteration.
    std::vector<std::pair<xrepl::StreamId, TabletId>> unfinished_stream_tablet;
    for (const auto& [stream_id, tablet_id] : stream_tablet_to_check) {
      auto tablet_peer = context_->LookupTablet(tablet_id);
      if (!tablet_peer || !tablet_peer->IsLeaderAndReady()) {
        LOG_WITH_FUNC(INFO) << "Not the leader for tablet " << tablet_id << ". Skipping.";
        continue;
      }

      ProducerTabletInfo producer_tablet = {{}, stream_id, tablet_id};
      auto s = CheckTabletValidForStream(producer_tablet);
      if (!s.ok()) {
        LOG_WITH_FUNC(WARNING) << "Tablet not valid for stream: " << s << ". Skipping.";
        continue;
      }

      auto tablet_metric = std::static_pointer_cast<CDCTabletMetrics>(
          GetCDCTabletMetrics(producer_tablet, tablet_peer));
      if (!tablet_metric) {
        LOG_WITH_FUNC(INFO) << "Tablet metrics uninitialized: " << producer_tablet.ToString();
        unfinished_stream_tablet.push_back({stream_id, tablet_id});
        continue;
      } else if (!tablet_metric->last_getchanges_time->value()) {
        LOG_WITH_FUNC(INFO) << "GetChanges never received: " << producer_tablet.ToString();
        unfinished_stream_tablet.push_back({stream_id, tablet_id});
        continue;
      }

      // Check if the consumer is caught-up to the user-specified timestamp.
      auto last_caughtup_time = tablet_metric->last_caughtup_physicaltime->value();
      if (req->target_time() <= last_caughtup_time) {
        auto drained_stream_info = resp->add_drained_stream_info();
        drained_stream_info->set_stream_id(stream_id.ToString());
        drained_stream_info->set_tablet_id(tablet_id);
      } else {
        unfinished_stream_tablet.push_back({stream_id, tablet_id});
      }
    }
    stream_tablet_to_check.swap(unfinished_stream_tablet);
  } while (sleep_while_unfinished());

  context.RespondSuccess();
}

void CDCServiceImpl::AddTabletCheckpoint(
    OpId op_id, const xrepl::StreamId& stream_id, const TabletId& tablet_id) {
  impl_->AddTabletCheckpoint(op_id, stream_id, tablet_id);
}

bool CDCServiceImpl::ValidateAutoFlagsConfigVersion(
    const GetChangesRequestPB& req, GetChangesResponsePB& resp, rpc::RpcContext& context) {
  if (!FLAGS_enable_xcluster_auto_flag_validation || !req.has_auto_flags_config_version()) {
    return true;
  }

  const auto get_auto_flags_version_result = context_->GetAutoFlagsConfigVersion();

  if (!get_auto_flags_version_result) {
    SetupErrorAndRespond(
        resp.mutable_error(), get_auto_flags_version_result.status(), CDCErrorPB::INTERNAL_ERROR,
        &context);
    return false;
  }
  const auto local_auto_flags_config_version = *get_auto_flags_version_result;

  if (local_auto_flags_config_version == req.auto_flags_config_version()) {
    return true;
  }

  resp.set_auto_flags_config_version(local_auto_flags_config_version);
  SetupErrorAndRespond(
      resp.mutable_error(),
      STATUS_FORMAT(
          InvalidArgument, "AutoFlags config version mismatch. Expected: $0, Actual: $1",
          req.auto_flags_config_version(), local_auto_flags_config_version),
      CDCErrorPB::AUTO_FLAGS_CONFIG_VERSION_MISMATCH, &context);
  return false;
}

}  // namespace cdc
}  // namespace yb
