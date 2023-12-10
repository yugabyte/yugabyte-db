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

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/cdc/cdc_producer.h"
#include "yb/cdc/cdc_rpc.h"
#include "yb/cdc/cdc_service.proxy.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/entity_ids.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
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

#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stol_utils.h"
#include "yb/util/thread.h"
#include "yb/util/trace.h"

#include "yb/yql/cql/ql/util/statement_result.h"

constexpr uint32_t kUpdateIntervalMs = 15 * 1000;

DEFINE_int32(cdc_read_rpc_timeout_ms, 30 * 1000,
             "Timeout used for CDC read rpc calls.  Reads normally occur cross-cluster.");
TAG_FLAG(cdc_read_rpc_timeout_ms, advanced);

DEFINE_int32(cdc_write_rpc_timeout_ms, 30 * 1000,
             "Timeout used for CDC write rpc calls.  Writes normally occur intra-cluster.");
TAG_FLAG(cdc_write_rpc_timeout_ms, advanced);

DEFINE_int32(cdc_ybclient_reactor_threads, 50,
             "The number of reactor threads to be used for processing ybclient "
             "requests for CDC.");
TAG_FLAG(cdc_ybclient_reactor_threads, advanced);

DEFINE_int32(cdc_state_checkpoint_update_interval_ms, kUpdateIntervalMs,
             "Rate at which CDC state's checkpoint is updated.");

DEFINE_string(certs_for_cdc_dir, "",
              "The parent directory of where all certificates for xCluster producer universes will "
              "be stored, for when the producer and consumer clusters use different certificates. "
              "Place the certificates for each producer cluster in "
              "<certs_for_cdc_dir>/<producer_cluster_id>/*.");

DEFINE_int32(update_min_cdc_indices_interval_secs, 60,
             "How often to read cdc_state table to get the minimum applied index for each tablet "
             "across all streams. This information is used to correctly keep log files that "
             "contain unapplied entries. This is also the rate at which a tablet's minimum "
             "replicated index across all streams is sent to the other peers in the configuration. "
             "If flag enable_log_retention_by_op_idx is disabled, this flag has no effect.");

DEFINE_int32(update_metrics_interval_ms, kUpdateIntervalMs,
             "How often to update xDC cluster metrics.");

DEFINE_bool(enable_cdc_state_table_caching, true, "Enable caching the cdc_state table schema.");

DEFINE_bool(enable_cdc_client_tablet_caching, false, "Enable caching the tablets found by client.");

DEFINE_bool(enable_collect_cdc_metrics, true, "Enable collecting cdc metrics.");

DEFINE_double(cdc_read_safe_deadline_ratio, .10,
              "When the heartbeat deadline has this percentage of time remaining, "
              "the master should halt tablet report processing so it can respond in time.");

DEFINE_double(cdc_get_changes_free_rpc_ratio, .10,
              "When the TServer only has this percentage of RPCs remaining because the rest are "
              "GetChanges, reject additional requests to throttle/backoff and prevent deadlocks.");

DEFINE_bool(enable_update_local_peer_min_index, false,
            "Enable each local peer to update its own log checkpoint instead of the leader "
            "updating all peers.");

DEFINE_bool(parallelize_bootstrap_producer, true,
            "When this is true, use the version of BootstrapProducer with batched and "
            "parallelized rpc calls. This is recommended for large input sizes");

DEFINE_test_flag(uint64, cdc_log_init_failure_timeout_seconds, 0,
    "Timeout in seconds for CDCServiceImpl::SetCDCCheckpoint to return log init failure");

DECLARE_bool(enable_log_retention_by_op_idx);

DECLARE_int32(cdc_checkpoint_opid_interval_ms);

DECLARE_int32(rpc_workers_limit);

DECLARE_int64(cdc_intent_retention_ms);

METRIC_DEFINE_entity(cdc);

namespace yb {
namespace cdc {

using namespace std::literals;

using rpc::RpcContext;
using tserver::TSTabletManager;
using client::internal::RemoteTabletServer;

constexpr int kMaxDurationForTabletLookup = 50;
const client::YBTableName kCdcStateTableName(
    YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);

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

  const TabletId& tablet_id() const {
    return producer_tablet_info.tablet_id;
  }

  const CDCStreamId& stream_id() const {
    return producer_tablet_info.stream_id;
  }
};

struct CDCStateMetadataInfo {
  ProducerTabletInfo producer_tablet_info;

  mutable std::string commit_timestamp;
  mutable std::shared_ptr<Schema>  current_schema;
  mutable OpId last_streamed_op_id;
  mutable SchemaVersion current_schema_version;

  std::shared_ptr<MemTracker> mem_tracker;

  const TableId& tablet_id() const {
    return producer_tablet_info.tablet_id;
  }

  const CDCStreamId& stream_id() const {
    return producer_tablet_info.stream_id;
  }

};

class TabletTag;
class StreamTag;

using TabletCheckpoints = boost::multi_index_container <
    TabletCheckpointInfo,
    boost::multi_index::indexed_by <
        boost::multi_index::hashed_unique <
            boost::multi_index::member <
                TabletCheckpointInfo, ProducerTabletInfo,
                &TabletCheckpointInfo::producer_tablet_info
            >
        >,
        boost::multi_index::hashed_non_unique <
            boost::multi_index::tag <TabletTag>,
            boost::multi_index::const_mem_fun <
                TabletCheckpointInfo, const TabletId&, &TabletCheckpointInfo::tablet_id
            >
        >,
        boost::multi_index::hashed_non_unique <
            boost::multi_index::tag <StreamTag>,
            boost::multi_index::const_mem_fun <
                TabletCheckpointInfo, const CDCStreamId&, &TabletCheckpointInfo::stream_id
            >
        >
    >
>;

using CDCStateMetadata = boost::multi_index_container <
    CDCStateMetadataInfo,
    boost::multi_index::indexed_by <
        boost::multi_index::hashed_unique <
            boost::multi_index::member <
                CDCStateMetadataInfo, ProducerTabletInfo,
                &CDCStateMetadataInfo::producer_tablet_info>
        >,
        boost::multi_index::hashed_non_unique <
            boost::multi_index::tag <TabletTag>,
            boost::multi_index::const_mem_fun <
                CDCStateMetadataInfo, const TabletId&, &CDCStateMetadataInfo::tablet_id
            >
        >,
        boost::multi_index::hashed_non_unique <
            boost::multi_index::tag <StreamTag>,
            boost::multi_index::const_mem_fun <
                CDCStateMetadataInfo, const CDCStreamId&, &CDCStateMetadataInfo::stream_id
            >
        >
    >
>;

} // namespace

class CDCServiceImpl::Impl {
 public:
  explicit Impl(TSTabletManager* tablet_manager, rw_spinlock* mutex) : mutex_(*mutex) {
    const auto server = tablet_manager->server();
    async_client_init_.emplace(
        "cdc_client", std::chrono::milliseconds(FLAGS_cdc_read_rpc_timeout_ms),
        server->permanent_uuid(), &server->options(), server->metric_entity(),
        server->mem_tracker(), server->messenger());
    async_client_init_->Start();
  }

  void UpdateCDCStateMetadata(
      const ProducerTabletInfo& producer_tablet,
      const std::string& timestamp,
      const std::shared_ptr<Schema>& schema,
      const OpId& op_id,
      const uint32_t current_schema_version) {
    std::lock_guard<decltype(mutex_)> l(mutex_);
    auto it = cdc_state_metadata_.find(producer_tablet);
    if (it == cdc_state_metadata_.end()) {
      LOG(DFATAL) << "Failed to update the cdc state metadata for tablet id: "
                  << producer_tablet.tablet_id;
      return;
    }
    it->commit_timestamp = timestamp;
    it->current_schema = schema;
    it->last_streamed_op_id = op_id;
    it->current_schema_version = current_schema_version;
  }

  std::pair<uint32_t, std::shared_ptr<Schema>> GetOrAddSchema(
      const ProducerTabletInfo& producer_tablet, const bool need_schema_info) {
    std::lock_guard<decltype(mutex_)> l(mutex_);
    auto it = cdc_state_metadata_.find(producer_tablet);

    if (it != cdc_state_metadata_.end()) {
      if (need_schema_info) {
        it->current_schema = std::make_shared<Schema>();
        it->current_schema_version = std::numeric_limits<uint32_t>::max();
      }
      return make_pair(it->current_schema_version, it->current_schema);
    }
    CDCStateMetadataInfo info = CDCStateMetadataInfo {
        .producer_tablet_info = producer_tablet,
        .commit_timestamp = {},
        .current_schema = std::make_shared<Schema>(),
        .last_streamed_op_id = OpId(),
        .current_schema_version = std::numeric_limits<uint32_t>::max(),
        .mem_tracker = nullptr,
    };
    cdc_state_metadata_.emplace(info);
    return {info.current_schema_version, info.current_schema};
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
      OpId op_id,
      const CDCStreamId& stream_id,
      const TabletId& tablet_id,
      std::vector<ProducerTabletInfo>* producer_entries_modified) {
    ProducerTabletInfo producer_tablet{
      .universe_uuid = "",
      .stream_id = stream_id,
      .tablet_id = tablet_id
    };
    CoarseTimePoint time;
    int64_t active_time;

    if (producer_entries_modified) {
      producer_entries_modified->push_back(producer_tablet);
      time = CoarseMonoClock::Now();
      active_time = GetCurrentTimeMicros();
    } else {
      time = CoarseTimePoint::min();
      active_time = 0;
    }
    std::lock_guard<decltype(mutex_)> l(mutex_);
    if (!producer_entries_modified && tablet_checkpoints_.count(producer_tablet)) {
      return;
    }
    tablet_checkpoints_.emplace(TabletCheckpointInfo{
        .producer_tablet_info = producer_tablet,
        .cdc_state_checkpoint = {op_id, time, active_time},
        .sent_checkpoint = {op_id, time, active_time},
        .mem_tracker = nullptr});
  }

  void EraseTablets(const std::vector<ProducerTabletInfo>& producer_entries_modified,
                    bool erase_cdc_states)
      NO_THREAD_SAFETY_ANALYSIS {
    for (const auto& entry : producer_entries_modified) {
      tablet_checkpoints_.get<TabletTag>().erase(entry.tablet_id);
      if (erase_cdc_states) {
        cdc_state_metadata_.get<TabletTag>().erase(entry.tablet_id);
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
    std::lock_guard<rw_spinlock> l(mutex_);
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

  bool UpdateCheckpoint(const ProducerTabletInfo& producer_tablet,
                        const OpId& sent_op_id,
                        const OpId& commit_op_id) {
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

    std::lock_guard<decltype(mutex_)> l(mutex_);
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
    std::lock_guard<rw_spinlock> l(mutex_);
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
    it->mem_tracker = MemTracker::FindOrCreateTracker(producer_info.stream_id, cdc_mem_tracker);
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
      const std::array<const master::TabletLocationsPB*, 2>& tablets,
      const OpId& split_op_id) {
    std::lock_guard<rw_spinlock> l(mutex_);
    for (const auto& tablet : tablets) {
      ProducerTabletInfo producer_info{info.universe_uuid, info.stream_id, tablet->tablet_id()};
      tablet_checkpoints_.emplace(TabletCheckpointInfo{
          .producer_tablet_info = producer_info,
          .cdc_state_checkpoint =
              TabletCheckpoint{
                  .op_id = split_op_id, .last_update_time = {}, .last_active_time = {}},
          .sent_checkpoint =
              TabletCheckpoint{
                  .op_id = split_op_id, .last_update_time = {}, .last_active_time = {}},
          .mem_tracker = nullptr,
      });
      cdc_state_metadata_.emplace(CDCStateMetadataInfo{
          .producer_tablet_info = producer_info,
          .commit_timestamp = {},
          .current_schema = std::make_shared<Schema>(),
          .last_streamed_op_id = split_op_id,
          .current_schema_version = std::numeric_limits<uint32_t>::max(),
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
      std::lock_guard<rw_spinlock> l(mutex_);
      for (const auto &tablet : tablets) {
        // Add every tablet in the stream.
        ProducerTabletInfo producer_info{info.universe_uuid, info.stream_id, tablet.tablet_id()};
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
            .current_schema = std::make_shared<Schema>(),
            .last_streamed_op_id = OpId(),
            .current_schema_version = std::numeric_limits<uint32_t>::max(),
            .mem_tracker = nullptr,
        });
        // If this is the tablet that the user requested.
        if (tablet.tablet_id() == info.tablet_id) {
          found = true;
        }
      }
    }
    return found ? Status::OK()
                 : STATUS_FORMAT(InvalidArgument, "Tablet ID $0 is not part of stream ID $1",
                                 info.tablet_id, info.stream_id);
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
    std::lock_guard<rw_spinlock> l(mutex_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      // Setting the timestamp to min will result in ExpiredAt saying it is expired.
      it->cdc_state_checkpoint.last_update_time = CoarseTimePoint::min();
    }
  }

  void ClearCaches() {
    std::lock_guard<rw_spinlock> l(mutex_);
    tablet_checkpoints_.clear();
    cdc_state_metadata_.clear();
  }

  boost::optional<client::AsyncClientInitialiser> async_client_init_;

  // this will be used for the std::call_once call while caching the client
  std::once_flag is_client_cached_;
 private:
  rw_spinlock& mutex_;

  TabletCheckpoints tablet_checkpoints_ GUARDED_BY(mutex_);

  CDCStateMetadata cdc_state_metadata_ GUARDED_BY(mutex_);
};

CDCServiceImpl::CDCServiceImpl(TSTabletManager* tablet_manager,
                               const scoped_refptr<MetricEntity>& metric_entity_server,
                               MetricRegistry* metric_registry)
    : CDCServiceIf(metric_entity_server),
      tablet_manager_(tablet_manager),
      metric_registry_(metric_registry),
      server_metrics_(std::make_shared<CDCServerMetrics>(metric_entity_server)),
      get_changes_rpc_sem_(std::max(1.0, floor(
          FLAGS_rpc_workers_limit * (1 - FLAGS_cdc_get_changes_free_rpc_ratio)))),
      impl_(new Impl(tablet_manager, &mutex_)) {
  CHECK_OK(Thread::Create(
      "cdc_service", "update_peers_and_metrics", &CDCServiceImpl::UpdatePeersAndMetrics, this,
      &update_peers_and_metrics_thread_));
  LOG_IF(WARNING, get_changes_rpc_sem_.GetValue() == 1) << "only 1 thread available for GetChanges";
}

CDCServiceImpl::~CDCServiceImpl() {
  Shutdown();
}

client::YBClient* CDCServiceImpl::client() {
  return impl_->async_client_init_->client();
}

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

bool IsTabletPeerLeader(const std::shared_ptr<tablet::TabletPeer>& peer) {
  return peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
}

std::unordered_map<std::string, std::string> GetCreateCDCStreamOptions(
    const CreateCDCStreamRequestPB* req) {
  std::unordered_map<std::string, std::string> options;
  if(req->has_namespace_name()) {
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

Status DoUpdateCDCConsumerOpId(const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                               const OpId& checkpoint,
                               const TabletId& tablet_id) {
  std::shared_ptr<consensus::Consensus> shared_consensus = tablet_peer->shared_consensus();

  if (shared_consensus == nullptr) {
    return STATUS_FORMAT(InternalError,
                         "Failed to get tablet $0 peer consensus", tablet_id);
  }

  shared_consensus->UpdateCDCConsumerOpId(checkpoint);
  return Status::OK();
}

bool UpdateCheckpointRequired(
    const StreamMetadata& record, const CDCSDKCheckpointPB& cdc_sdk_op_id, bool* force_update) {
  switch (record.source_type) {
    case XCLUSTER:
      return true;

    case CDCSDK:
      if (cdc_sdk_op_id.write_id() == 0) {
        return true;
      }
      if (cdc_sdk_op_id.write_id() == -1) {
        // CDC should do a force update of checkpoint in cdc_state table as a part snapshot
        // initialization, so that it can restrict intents and WALS GC if concurrent DML operations
        // are running as well as UpdatePeersAndMetrics will not GC the intents and WALS for the
        // FOLLOWERS.
        if (cdc_sdk_op_id.key().empty() && cdc_sdk_op_id.snapshot_time() == 0) {
          *force_update = true;
        }
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
    return STATUS(InvalidArgument, "Cannot setup CDC on YEDIS_TABLE");;
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

  if(!req.has_stream_id()) {
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

// This function is to handle the upgrade scenario where the DB is upgraded from a version
// without CDCSDK changes to the one with it. So in case, some required options are missing,
// the default values will be added for the same.
void AddDefaultOptionsIfMissing(std::unordered_map<std::string, std::string>* options) {
  if ((*options).find(cdc::kSourceType) == (*options).end()) {
    (*options).emplace(cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::XCLUSTER));
  }

  if ((*options).find(cdc::kCheckpointType) == (*options).end()) {
    (*options).emplace(cdc::kCheckpointType,
                     CDCCheckpointType_Name(cdc::CDCCheckpointType::IMPLICIT));
  }
}

} // namespace

template <class ReqType, class RespType>
bool CDCServiceImpl::CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc) {
  TRACE("Received RPC $0: $1", rpc->ToString(), req->DebugString());
  if (PREDICT_FALSE(!tablet_manager_)) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(ServiceUnavailable, "Tablet Server is not running"),
                         CDCErrorPB::NOT_RUNNING,
                         rpc);
    return false;
  }
  return true;
}

void CDCServiceImpl::CreateEntryInCdcStateTable(
    const std::shared_ptr<client::TableHandle>& cdc_state_table,
    std::vector<ProducerTabletInfo>* producer_entries_modified,
    std::vector<client::YBOperationPtr>* ops,
    const CDCStreamId& stream_id,
    const TabletId& tablet_id,
    const OpId& op_id) {
  // For CDCSDK the initial checkpoint for each tablet will be maintained
  // in cdc_state table as -1.-1(Invalid), which is the default value of 'op_id'. Checkpoint will be
  // updated when client call setCDCCheckpoint.
  const auto cdc_state_table_op = cdc_state_table->NewWriteOp(
      QLWriteRequestPB::QL_STMT_INSERT);
  auto *const cdc_state_table_write_req = cdc_state_table_op->mutable_request();

  QLAddStringHashValue(cdc_state_table_write_req, tablet_id);
  QLAddStringRangeValue(cdc_state_table_write_req, stream_id);
  cdc_state_table->AddStringColumnValue(cdc_state_table_write_req,
                                        master::kCdcCheckpoint, op_id.ToString());
  auto column_id = cdc_state_table->ColumnId(master::kCdcData);
  cdc_state_table->AddMapColumnValue(cdc_state_table_write_req, column_id, "active_time", "0");
  ops->push_back(std::move(cdc_state_table_op));

  impl_->AddTabletCheckpoint(op_id, stream_id, tablet_id, producer_entries_modified);
}

Result<NamespaceId> CDCServiceImpl::GetNamespaceId(const std::string& ns_name) {
  master::GetNamespaceInfoResponsePB namespace_info_resp;
  RETURN_NOT_OK(client()->GetNamespaceInfo(std::string(),
                                           ns_name,
                                           YQL_DATABASE_PGSQL,
                                           &namespace_info_resp));

  return namespace_info_resp.namespace_().id();
}

Result<EnumOidLabelMap> CDCServiceImpl::GetEnumMapFromCache(const NamespaceName& ns_name) {
  {
    yb::SharedLock<decltype(mutex_)> l(mutex_);
    if (enumlabel_cache_.find(ns_name) != enumlabel_cache_.end()) {
      return enumlabel_cache_.at(ns_name);
    }
  }
  return UpdateCacheAndGetEnumMap(ns_name);
}

Result<EnumOidLabelMap> CDCServiceImpl::UpdateCacheAndGetEnumMap(const NamespaceName& ns_name) {
  std::lock_guard<decltype(mutex_)> l(mutex_);
  if (enumlabel_cache_.find(ns_name) == enumlabel_cache_.end()) {
    return UpdateEnumMapInCacheUnlocked(ns_name);
  }
  return enumlabel_cache_.at(ns_name);
}

Result<EnumOidLabelMap> CDCServiceImpl::UpdateEnumMapInCacheUnlocked(const NamespaceName& ns_name) {
  EnumOidLabelMap enum_oid_label_map = VERIFY_RESULT(client()->GetPgEnumOidLabelMap(ns_name));
  enumlabel_cache_[ns_name] = enum_oid_label_map;
  return enumlabel_cache_[ns_name];
}

Status CDCServiceImpl::CreateCDCStreamForNamespace(
    const CreateCDCStreamRequestPB* req,
    CreateCDCStreamResponsePB* resp,
    CoarseTimePoint deadline) {
  auto session = client()->NewSession();

  // Used to delete streams in case of failure.
  CDCCreationState creation_state;

  auto scope_exit = ScopeExit([this, &creation_state] {
    RollbackPartialCreate(creation_state);
  });

  auto ns_id = VERIFY_RESULT_OR_SET_CODE(
      GetNamespaceId(req->namespace_name()), CDCError(CDCErrorPB::INVALID_REQUEST));

  // Generate a stream id by calling CreateCDCStream, and also setup the stream in the master.
  std::unordered_map<std::string, std::string> options = GetCreateCDCStreamOptions(req);

  CDCStreamId db_stream_id = VERIFY_RESULT_OR_SET_CODE(
      client()->CreateCDCStream(ns_id, options, false /*active*/),
      CDCError(CDCErrorPB::INTERNAL_ERROR));

  auto table_list = VERIFY_RESULT_OR_SET_CODE(
      client()->ListUserTables(ns_id), CDCError(CDCErrorPB::INTERNAL_ERROR));

  options.erase(kIdType);

  std::vector<client::YBOperationPtr> ops;
  std::vector<TableId> table_ids;
  std::vector<CDCStreamId> stream_ids;

  auto cdc_state_table =
      VERIFY_RESULT_OR_SET_CODE(GetCdcStateTable(), CDCError(CDCErrorPB::INTERNAL_ERROR));

  for (const auto& table_iter : table_list) {
    std::shared_ptr<client::YBTable> table;

    RETURN_NOT_OK_SET_CODE(
        client()->OpenTable(table_iter.table_id(), &table), CDCError(CDCErrorPB::TABLE_NOT_FOUND));

    // internally if any of the table doesn't have a primary key, then do not create
    // a CDC stream ID for that table
    if (!YsqlTableHasPrimaryKey(table->schema())) {
      LOG(WARNING) << "Skipping CDC stream creation on " << table->name().table_name()
                   << " because it does not have a primary key";
      continue;
    }

    // We don't allow CDC on YEDIS and tables without a primary key.
    if (req->record_format() != CDCRecordFormat::WAL) {
      RETURN_NOT_OK_SET_CODE(CheckCdcCompatibility(table), CDCError(CDCErrorPB::INVALID_REQUEST));
    }

    // We only change the stream's state to "ACTIVE", while we are inserting the last table for the
    // stream.
    bool set_active = table_iter == table_list.back();

    const CDCStreamId stream_id = VERIFY_RESULT_OR_SET_CODE(
        client()->CreateCDCStream(table_iter.table_id(), options, set_active, db_stream_id),
        CDCError(CDCErrorPB::INTERNAL_ERROR));

    creation_state.created_cdc_streams.push_back(stream_id);

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK_SET_CODE(
        client()->GetTabletsFromTableId(table_iter.table_id(), 0, &tablets),
        CDCError(CDCErrorPB::TABLE_NOT_FOUND));

    // For each tablet, create a row in cdc_state table containing the generated stream id, and
    // the op id as max in the logs.
    for (const auto& tablet : tablets) {
      CreateEntryInCdcStateTable(
          cdc_state_table,
          &creation_state.producer_entries_modified,
          &ops,
          db_stream_id,
          tablet.tablet_id());
    }
    stream_ids.push_back(std::move(stream_id));
    table_ids.push_back(table_iter.table_id());
  }

  // Add stream to cache.
  AddStreamMetadataToCache(
      db_stream_id,
      std::make_shared<StreamMetadata>(
          ns_id, table_ids, req->record_type(), req->record_format(), req->source_type(),
          req->checkpoint_type()));

  session->SetDeadline(deadline);

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_SET_CODE(
      RefreshCacheOnFail(session->TEST_ApplyAndFlush(ops)), CDCError(CDCErrorPB::INTERNAL_ERROR));

  resp->set_db_stream_id(db_stream_id);

  // Clear creation_state so no changes are reversed by scope_exit since we succeeded.
  creation_state.Clear();

  return Status::OK();
}

void CDCServiceImpl::CreateCDCStream(const CreateCDCStreamRequestPB* req,
                                     CreateCDCStreamResponsePB* resp,
                                     RpcContext context) {
  CDCStreamId streamId;

  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(req->has_table_id() || req->has_namespace_name(),
                             STATUS(InvalidArgument,
                                    "Table ID or Database name is required to create CDC stream"),
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

    auto result = client()->CreateCDCStream(req->table_id(), options);
    RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                               CDCErrorPB::INTERNAL_ERROR, context);

    resp->set_stream_id(*result);

    // Add stream to cache.
    AddStreamMetadataToCache(
        *result, std::make_shared<StreamMetadata>(
                     "",
                     std::vector<TableId>{req->table_id()},
                     req->record_type(),
                     req->record_format(),
                     req->source_type(),
                     req->checkpoint_type()));
  } else if (req->has_namespace_name()) {
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

  auto record = VERIFY_RESULT(GetStream(req.stream_id()));
  if (record->checkpoint_type != EXPLICIT) {
    LOG(WARNING) << "Setting the checkpoint explicitly even though the checkpoint type is implicit";
  }

  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  auto s = tablet_manager_->GetTabletPeer(req.tablet_id(), &tablet_peer);

  // Case-1 The connected tserver does not contain the requested tablet_id.
  // Case-2 The connected tserver does not contain the tablet LEADER.
  if ((!tablet_peer || IsTabletPeerLeader(tablet_peer)) && req.serve_as_proxy()) {
    // Get tablet LEADER.
    auto result = GetLeaderTServer(req.tablet_id(), false /* use_cache */);
    RETURN_NOT_OK_SET_CODE(result, CDCError(CDCErrorPB::NOT_LEADER));
    auto ts_leader = *result;
    auto cdc_proxy = GetCDCServiceProxy(ts_leader);

    SetCDCCheckpointRequestPB new_req;
    new_req.CopyFrom(req);
    new_req.set_serve_as_proxy(false);

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));
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
  } else if (!s.ok() || IsTabletPeerLeader(tablet_peer)) {
    VLOG(2) << "Current LEADER is not ready to serve tablet_id: " << req.tablet_id();
    RETURN_NOT_OK_SET_CODE(s, CDCError(CDCErrorPB::LEADER_NOT_READY));
  }

  ProducerTabletInfo producer_tablet{"" /* UUID */, req.stream_id(), req.tablet_id()};
  RETURN_NOT_OK_SET_CODE(
      CheckTabletValidForStream(producer_tablet), CDCError(CDCErrorPB::INVALID_REQUEST));

  OpId checkpoint;
  bool set_latest_entry = req.bootstrap();

  if (set_latest_entry) {
    const string err_message = strings::Substitute(
        "Unable to get the latest entry op id from "
        "peer $0 and tablet $1 because its log object hasn't been initialized",
        tablet_peer->permanent_uuid(), tablet_peer->tablet_id());

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
        RETURN_NOT_OK_SET_CODE(
            STATUS(ServiceUnavailable, err_message), CDCError(CDCErrorPB::LEADER_NOT_READY));
      }
    }

    if (!tablet_peer->log_available()) {
      RETURN_NOT_OK_SET_CODE(
          STATUS(ServiceUnavailable, err_message), CDCError(CDCErrorPB::LEADER_NOT_READY));
    }
    checkpoint = tablet_peer->log()->GetLatestEntryOpId();
  } else {
    checkpoint = OpId::FromPB(req.checkpoint().op_id());
  }
  auto session = client()->NewSession();
  session->SetDeadline(deadline);

  RETURN_NOT_OK_SET_CODE(
      UpdateCheckpointAndActiveTime(
          producer_tablet, checkpoint, checkpoint, session, GetCurrentTimeMicros(),
          CDCRequestSource::CDCSDK, true),
      CDCError(CDCErrorPB::INTERNAL_ERROR));

  if (req.has_initial_checkpoint() || set_latest_entry) {
    auto result = SetInitialCheckPoint(checkpoint, req.tablet_id(), tablet_peer);
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

  // Look up stream in sys catalog.
  std::vector<TableId> table_ids;
  NamespaceId ns_id;
  std::unordered_map<std::string, std::string> options;
  RPC_STATUS_RETURN_ERROR(
      client()->GetCDCStream(req->table_info().stream_id(), &ns_id, &table_ids, &options),
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

    std::vector<std::pair<TabletId, OpId>> tablet_checkpoint_pairs;
    RPC_STATUS_RETURN_ERROR(
        GetTabletIdsToPoll(
            req->table_info().stream_id(), active_or_hidden_tablets, parent_tablets,
            child_to_parent_mapping, &tablet_checkpoint_pairs),
        resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

    resp->mutable_tablet_checkpoint_pairs()->Reserve(
        static_cast<int>(tablet_checkpoint_pairs.size()));
    for (auto tablet_checkpoint_pair : tablet_checkpoint_pairs) {
      auto tablet_checkpoint_pair_pb = resp->add_tablet_checkpoint_pairs();

      tablet_checkpoint_pair_pb->mutable_tablet_locations()->CopyFrom(
          tablet_id_to_tablet_locations_map[tablet_checkpoint_pair.first]);
      CDCSDKCheckpointPB checkpoint_pb;
      tablet_checkpoint_pair.second.ToPB(&checkpoint_pb);
      tablet_checkpoint_pair_pb->mutable_cdc_sdk_checkpoint()->CopyFrom(checkpoint_pb);
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
      auto session = client()->NewSession();
      ProducerTabletInfo parent_tablet = {
          "" /* UUID */, req->table_info().stream_id(), req->tablet_id()};
      auto result = GetLastCheckpoint(parent_tablet, session);
      if (!result.ok()) {
        SetupErrorAndRespond(resp->mutable_error(), result.status(),
                             CDCErrorPB::INTERNAL_ERROR, &context);
      }
      (*result).ToPB(&parent_checkpoint_pb);
    }

    for (const auto& child_tablet_id : child_tablet_ids) {
      auto session = client()->NewSession();
      ProducerTabletInfo cur_child_tablet = {
          "" /* UUID */, req->table_info().stream_id(), child_tablet_id};

      auto tablet_checkpoint_pair_pb = resp->add_tablet_checkpoint_pairs();
      tablet_checkpoint_pair_pb->mutable_tablet_locations()->CopyFrom(
          tablet_id_to_tablet_locations_map[child_tablet_id]);

      auto result = GetLastCheckpoint(cur_child_tablet, session);
      if (!result.ok()) {
        SetupErrorAndRespond(resp->mutable_error(), result.status(),
                             CDCErrorPB::INTERNAL_ERROR, &context);
      }
      if (result->is_valid_not_empty()) {
        CDCSDKCheckpointPB checkpoint_pb;
        (*result).ToPB(&checkpoint_pb);
        tablet_checkpoint_pair_pb->mutable_cdc_sdk_checkpoint()->CopyFrom(checkpoint_pb);
      } else {
        // Reuse the checkpoint from the parent.
        tablet_checkpoint_pair_pb->mutable_cdc_sdk_checkpoint()->CopyFrom(parent_checkpoint_pb);
      }
    }
  }

  context.RespondSuccess();
}

void CDCServiceImpl::DeleteCDCStream(const DeleteCDCStreamRequestPB* req,
                                     DeleteCDCStreamResponsePB* resp,
                                     RpcContext context) {
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

  vector<CDCStreamId> streams(req->stream_id().begin(), req->stream_id().end());
  Status s = client()->DeleteCDCStream(
        streams,
        (req->has_force_delete() && req->force_delete()),
        (req->has_ignore_errors() && req->ignore_errors()));
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  context.RespondSuccess();
}

void CDCServiceImpl::ListTablets(const ListTabletsRequestPB* req,
                                 ListTabletsResponsePB* resp,
                                 RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(req->has_stream_id(),
                             STATUS(InvalidArgument, "Stream ID is required to list tablets"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  auto tablets = GetTablets(req->stream_id());
  RPC_CHECK_AND_RETURN_ERROR(tablets.ok(), tablets.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);

  if (!req->local_only()) {
    resp->mutable_tablets()->Reserve(tablets->size());
  }

  for (const auto& tablet : *tablets) {
    // Filter local tablets if needed.
    if (req->local_only()) {
      bool is_local = false;
      for (const auto& replica : tablet.replicas()) {
        if (replica.ts_info().permanent_uuid() == tablet_manager_->server()->permanent_uuid()) {
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
      auto tserver =  res->add_tservers();
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
    const CDCStreamId& stream_id) {
  auto stream_metadata = VERIFY_RESULT(GetStream(stream_id, /*ignore_cache*/ true));
  client::YBTableName table_name;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> all_tablets;

  for (const auto& table_id : stream_metadata->table_ids) {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    table_name.set_table_id(table_id);
    RETURN_NOT_OK(client()->GetTablets(
        table_name, 0, &tablets, /* partition_list_version =*/nullptr,
        RequireTabletsRunning::kFalse, master::IncludeInactive::kTrue));

    all_tablets.MergeFrom(tablets);
  }

  return all_tablets;
}

Result<TabletCheckpoint> CDCServiceImpl::TEST_GetTabletInfoFromCache(
    const ProducerTabletInfo& producer_tablet) {
  return impl_->TEST_GetTabletInfoFromCache(producer_tablet);
}

void CDCServiceImpl::GetChanges(const GetChangesRequestPB* req,
                                GetChangesResponsePB* resp,
                                RpcContext context) {
  if (!get_changes_rpc_sem_.TryAcquire()) {
    SetupErrorAndRespond(resp->mutable_error(), STATUS(LeaderNotReadyToServe, "Not ready to serve"),
                         CDCErrorPB::LEADER_NOT_READY, &context);
    return;
  }
  auto scope_exit = ScopeExit([this] {
    get_changes_rpc_sem_.Release();
  });

  if (!CheckOnline(req, resp, &context)) {
    return;
  }
  YB_LOG_EVERY_N_SECS(INFO, 300) << "Received GetChanges request " << req->ShortDebugString();

  RPC_CHECK_AND_RETURN_ERROR(req->has_tablet_id(),
                             STATUS(InvalidArgument, "Tablet ID is required to get CDC changes"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);
  RPC_CHECK_AND_RETURN_ERROR(req->has_stream_id() || req->has_db_stream_id(),
                             STATUS(InvalidArgument,
                             "Stream ID/DB Stream ID is required to get CDC changes"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  ProducerTabletInfo producer_tablet;
  CDCStreamId stream_id = req->has_db_stream_id() ? req->db_stream_id() : req->stream_id();

  auto session = client()->NewSession();
  CoarseTimePoint deadline = GetDeadline(context, client());
  session->SetDeadline(deadline);

  // Check that requested tablet_id is part of the CDC stream.
  producer_tablet = {"" /* UUID */, stream_id, req->tablet_id()};

  Status s = CheckTabletValidForStream(producer_tablet);
  if (!s.ok()) {
    RPC_STATUS_RETURN_ERROR(
        CheckTabletValidForStream(producer_tablet), resp->mutable_error(),
        s.IsTabletSplit() ? CDCErrorPB::TABLET_SPLIT : CDCErrorPB::INVALID_REQUEST, context);
  }

  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  s = tablet_manager_->GetTabletPeer(req->tablet_id(), &tablet_peer);
  auto original_leader_term = tablet_peer ? tablet_peer->LeaderTerm() : OpId::kUnknownTerm;

  // If we can't serve this tablet...
  if (s.IsNotFound() || tablet_peer->LeaderStatus() != consensus::LeaderStatus::LEADER_AND_READY) {
    if (req->serve_as_proxy()) {
      // Forward GetChanges() to tablet leader. This commonly happens in Kubernetes setups.
      auto context_ptr = std::make_shared<RpcContext>(std::move(context));
      TabletLeaderGetChanges(req, resp, context_ptr, tablet_peer);
    // Otherwise, figure out the proper return code.
    } else if (s.IsNotFound()) {
      SetupErrorAndRespond(resp->mutable_error(), s, CDCErrorPB::TABLET_NOT_FOUND, &context);
    } else if (tablet_peer->LeaderStatus() == consensus::LeaderStatus::NOT_LEADER) {
      // TODO: we may be able to get some changes, even if we're not the leader.
      SetupErrorAndRespond(resp->mutable_error(),
          STATUS(NotFound, Format("Not leader for $0", req->tablet_id())),
          CDCErrorPB::TABLET_NOT_FOUND, &context);
    } else {
      SetupErrorAndRespond(resp->mutable_error(),
          STATUS(LeaderNotReadyToServe, "Not ready to serve"),
          CDCErrorPB::LEADER_NOT_READY, &context);
    }
    return;
  }

  auto res = GetStream(stream_id);
  RPC_CHECK_AND_RETURN_ERROR(res.ok(), res.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);
  StreamMetadata record = **res;

  if (record.source_type == CDCSDK) {
    auto result = CheckStreamActive(producer_tablet, session);
    RPC_STATUS_RETURN_ERROR(result, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
    impl_->UpdateActiveTime(producer_tablet);
  }
  // This is the leader tablet, so mark cdc as enabled.
  cdc_enabled_.store(true, std::memory_order_release);

  OpId op_id;
  CDCSDKCheckpointPB cdc_sdk_op_id;
  // Get opId from request.
  if (!GetFromOpId(req, &op_id, &cdc_sdk_op_id)) {
    auto result = GetLastCheckpoint(producer_tablet, session);
    RPC_CHECK_AND_RETURN_ERROR(
        result.ok(), result.status(), resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
    if (record.source_type == XCLUSTER) {
      op_id = *result;
    } else {
      // This is the initial checkpoint set in cdc_state table, during create of CDCSDK
      // create stream, so throw an exeception to client to call setCDCCheckpoint or  take Snapshot.
      if (*result == OpId::Invalid()) {
        SetupErrorAndRespond(
            resp->mutable_error(),
            STATUS_FORMAT(
                InvalidArgument,
                "Invalid checkpoint $0 for tablet $1. Hint: set checkpoint explicitly or take "
                    "snapshot",
                *result, req->tablet_id()),
            CDCErrorPB::INTERNAL_ERROR, &context);
        return;
      }
      result->ToPB(&cdc_sdk_op_id);
      op_id = OpId::FromPB(cdc_sdk_op_id);
    }
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
      STATUS(TimedOut, "Too close to rpc timeout to call GetChanges."),
      resp->mutable_error(),
      CDCErrorPB::INTERNAL_ERROR, context);

    // Calculate a safe deadline so that CdcProducer::GetChanges times out
    // 20% faster than CdcServiceImpl::GetChanges. This gives enough
    // time (unless timeouts are unrealistically small) for CdcServiceImpl::GetChanges
    // to finish post-processing and return the partial results without itself timing out.
    const auto safe_deadline = deadline -
      (FLAGS_cdc_read_rpc_timeout_ms * 1ms * FLAGS_cdc_read_safe_deadline_ratio);
    get_changes_deadline = ToCoarse(MonoTime::FromUint64(safe_deadline.time_since_epoch().count()));
  }

  bool report_tablet_split = false;
  // Read the latest changes from the Log.
  if (record.source_type == XCLUSTER) {
    s = cdc::GetChangesForXCluster(
        stream_id, req->tablet_id(), op_id, record, tablet_peer, session,
        std::bind(&CDCServiceImpl::UpdateChildrenTabletsOnSplitOp, this, producer_tablet,
        std::placeholders::_1, session), mem_tracker,
        &msgs_holder, resp, &last_readable_index, get_changes_deadline);
  } else {
    std::string commit_timestamp;
    OpId last_streamed_op_id;
    auto cached_schema_info = impl_->GetOrAddSchema(producer_tablet, req->need_schema_info());

    auto tablet_ptr_result = tablet_peer->shared_tablet_safe();
    RPC_RESULT_RETURN_ERROR(
        tablet_ptr_result, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
    auto tablet_ptr = *tablet_ptr_result;

    auto namespace_name = tablet_ptr->metadata()->namespace_name();
    auto& [cached_schema_version, cached_schema] = cached_schema_info;
    auto last_sent_checkpoint = impl_->GetLastStreamedOpId(producer_tablet);
    // If from_op_id is more than the last sent op_id, it may be the stale entry and tablet
    // LEADERship change may happen.
    if (last_sent_checkpoint == boost::none ||
        OpId::FromPB(cdc_sdk_op_id) > *last_sent_checkpoint) {
      VLOG(1) << "Stale entry in the cache, because last sent checkpoint: "
              << *last_sent_checkpoint << " less than from_op_id: " << OpId::FromPB(cdc_sdk_op_id)
              << ", get proper schema version from system catalog.";
      cached_schema = std::make_shared<Schema>();
      cached_schema_version = std::numeric_limits<uint32_t>::max();
    }
    auto enum_map_result = GetEnumMapFromCache(namespace_name);
    RPC_CHECK_AND_RETURN_ERROR(
        enum_map_result.ok(), enum_map_result.status(), resp->mutable_error(),
        CDCErrorPB::INTERNAL_ERROR, context);

    s = cdc::GetChangesForCDCSDK(
        req->stream_id(), req->tablet_id(), cdc_sdk_op_id, record, tablet_peer, mem_tracker,
        *enum_map_result, client(), &msgs_holder, resp, &commit_timestamp,
        &cached_schema, &cached_schema_version, &last_streamed_op_id, &last_readable_index,
        get_changes_deadline);
    // This specific error from the docdb_pgapi layer is used to identify enum cache entry is
    // out of date, hence we need to repopulate.
    if (s.IsCacheMissError()) {
      {
        // Recreate the enum cache entry for the corresponding namespace.
        std::lock_guard<decltype(mutex_)> l(mutex_);
        enum_map_result = UpdateEnumMapInCacheUnlocked(namespace_name);
        RPC_CHECK_AND_RETURN_ERROR(
            enum_map_result.ok(), enum_map_result.status(), resp->mutable_error(),
            CDCErrorPB::INTERNAL_ERROR, context);
      }
      // Clean all the records which got added in the resp, till the enum cache miss failure is
      // encountered.
      resp->clear_cdc_sdk_proto_records();
      s = cdc::GetChangesForCDCSDK(
          req->stream_id(), req->tablet_id(), cdc_sdk_op_id, record, tablet_peer, mem_tracker,
          *enum_map_result, client(), &msgs_holder, resp, &commit_timestamp,
          &cached_schema, &cached_schema_version, &last_streamed_op_id, &last_readable_index,
          get_changes_deadline);
    }
    // This specific error indicates that a tablet split occured on the tablet.
    if (s.IsTabletSplit()) {
      s = UpdateChildrenTabletsOnSplitOpForCDCSDK(producer_tablet);
      RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

      report_tablet_split = true;
    }

    impl_->UpdateCDCStateMetadata(
        producer_tablet, commit_timestamp, cached_schema, OpId::FromPB(resp->cdc_sdk_checkpoint()),
        cached_schema_version);
  }

  auto tablet_metric = GetCDCTabletMetrics(producer_tablet, tablet_peer);
  if (tablet_metric) {
    tablet_metric->is_bootstrap_required->set_value(s.IsNotFound());
  }

  RPC_STATUS_RETURN_ERROR(
      s,
      resp->mutable_error(),
      s.IsNotFound() ? CDCErrorPB::CHECKPOINT_TOO_OLD : CDCErrorPB::UNKNOWN_ERROR,
      context);

  // Verify leadership was maintained for the duration of the GetChanges() read.
  s = tablet_manager_->GetTabletPeer(req->tablet_id(), &tablet_peer);
  if (s.IsNotFound() || tablet_peer->LeaderStatus() != consensus::LeaderStatus::LEADER_AND_READY ||
      tablet_peer->LeaderTerm() != original_leader_term) {
    SetupErrorAndRespond(resp->mutable_error(),
        STATUS(NotFound, Format("Not leader for $0", req->tablet_id())),
        CDCErrorPB::TABLET_NOT_FOUND, &context);
    return;
  }

  // Store information about the last server read & remote client ACK.
  uint64_t last_record_hybrid_time = resp->records_size() > 0 ?
      resp->records(resp->records_size() - 1).time() : 0;

  if (record.checkpoint_type == IMPLICIT) {
    bool force_update = false;
    if (UpdateCheckpointRequired(record, cdc_sdk_op_id, &force_update)) {
      // This is the snapshot bootstrap operation, so taking the checkpoint from the resp.
      if (force_update) {
        op_id = OpId(resp->cdc_sdk_checkpoint().term(), resp->cdc_sdk_checkpoint().index());
        LOG(INFO) << "Snapshot bootstrapping is initiated, forcefully update the checkpoint: "
                  << op_id;
      }
      RPC_STATUS_RETURN_ERROR(
          UpdateCheckpointAndActiveTime(
              producer_tablet, OpId::FromPB(resp->checkpoint().op_id()), op_id, session,
              last_record_hybrid_time, record.source_type),
          resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
    }

    s = DoUpdateCDCConsumerOpId(tablet_peer,
                                impl_->GetMinSentCheckpointForTablet(req->tablet_id()),
                                req->tablet_id());

    RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
  }
  // Update relevant GetChanges metrics before handing off the Response.
  UpdateCDCTabletMetrics(resp, producer_tablet, tablet_peer, op_id, last_readable_index);

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

  context.RespondSuccess();
}

Status CDCServiceImpl::UpdatePeersCdcMinReplicatedIndex(
    const TabletId& tablet_id, const TabletCDCCheckpointInfo& cdc_checkpoint_min,
    bool ignore_failures) {
  std::vector<client::internal::RemoteTabletServer *> servers;
  RETURN_NOT_OK(GetTServers(tablet_id, &servers));

  for (const auto &server : servers) {
    if (server->IsLocal()) {
      // We modify our log directly. Avoid calling itself through the proxy.
      continue;
    }
    LOG(INFO) << "Modifying remote peer " << server->ToString();
    auto proxy = GetCDCServiceProxy(server);
    UpdateCdcReplicatedIndexRequestPB update_index_req;
    UpdateCdcReplicatedIndexResponsePB update_index_resp;
    update_index_req.add_tablet_ids(tablet_id);
    update_index_req.add_replicated_indices(cdc_checkpoint_min.cdc_op_id.index);
    update_index_req.add_replicated_terms(cdc_checkpoint_min.cdc_op_id.term);
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

void CDCServiceImpl::ComputeLagMetric(
    int64_t last_replicated_micros,
    int64_t metric_last_timestamp_micros,
    int64_t cdc_state_last_replication_time_micros,
    scoped_refptr<AtomicGauge<int64_t>>
        metric) {
  if (metric_last_timestamp_micros == 0) {
    // The tablet metric timestamp is uninitialized, so try to use last replicated time in cdc
    // state.
    if (cdc_state_last_replication_time_micros == 0) {
      // Last replicated time in cdc state is uninitialized as well, so set the metric value to
      // 0 and update later when we have a suitable lower bound.
      metric->set_value(0);
    } else {
      metric->set_value(last_replicated_micros - cdc_state_last_replication_time_micros);
    }
  } else {
    metric->set_value(last_replicated_micros - metric_last_timestamp_micros);
  }
}

void CDCServiceImpl::UpdateLagMetrics() {
  auto tablet_checkpoints = impl_->TabletCheckpointsCopy();

  auto cdc_state_table_result = GetCdcStateTable();
  if (!cdc_state_table_result.ok()) {
    // It is possible that this runs before the cdc_state table is created. This is
    // ok. It just means that this is the first time the cluster starts.
    YB_LOG_EVERY_N_SECS(WARNING, 30)
        << "Unable to open table " << kCdcStateTableName.table_name() << " for metrics update.";
    return;
  }

  std::unordered_set<ProducerTabletInfo, ProducerTabletInfo::Hash> tablets_in_cdc_state_table;
  client::TableIteratorOptions options;
  options.columns = std::vector<string>{
      master::kCdcTabletId, master::kCdcStreamId, master::kCdcLastReplicationTime};
  bool failed = false;
  options.error_handler = [&failed](const Status& status) {
    YB_LOG_EVERY_N_SECS(WARNING, 30) << "Scan of table " << kCdcStateTableName.table_name()
                                     << " failed: " << status << ". Could not update metrics.";
    failed = true;
  };
  // First go through tablets in the cdc_state table and update metrics for each one.
  for (const auto& row : client::TableRange(**cdc_state_table_result, options)) {
    auto tablet_id = row.column(master::kCdcTabletIdIdx).string_value();
    auto stream_id = row.column(master::kCdcStreamIdIdx).string_value();
    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    Status s = tablet_manager_->GetTabletPeer(tablet_id, &tablet_peer);
    if (s.IsNotFound()) {
      continue;
    }

    bool is_leader = (tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY);
    ProducerTabletInfo tablet_info = {"" /* universe_uuid */, stream_id, tablet_id};
    tablets_in_cdc_state_table.insert(tablet_info);
    auto tablet_metric =
        GetCDCTabletMetrics(tablet_info, tablet_peer, CreateCDCMetricsEntity{is_leader});
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
      const auto& timestamp_ql_value = row.column(2);
      auto cdc_state_last_replication_time_micros =
          !timestamp_ql_value.IsNull() ?
          timestamp_ql_value.timestamp_value().ToInt64() : 0;
      auto last_sent_micros = tablet_metric->last_read_physicaltime->value();
      ComputeLagMetric(last_replicated_micros, last_sent_micros,
                       cdc_state_last_replication_time_micros,
                       tablet_metric->async_replication_sent_lag_micros);
      auto last_committed_micros = tablet_metric->last_checkpoint_physicaltime->value();
      ComputeLagMetric(last_replicated_micros, last_committed_micros,
                       cdc_state_last_replication_time_micros,
                       tablet_metric->async_replication_committed_lag_micros);
    }
  }
  if (failed) {
    RefreshCdcStateTable();
    return;
  }

  // Now, go through tablets in tablet_checkpoints_ and set lag to 0 for all tablets we're no
  // longer replicating.
  for (const auto& checkpoint : tablet_checkpoints) {
    const ProducerTabletInfo& tablet_info = checkpoint.producer_tablet_info;
    if (tablets_in_cdc_state_table.find(tablet_info) == tablets_in_cdc_state_table.end()) {
      // We're no longer replicating this tablet, so set lag to 0.
      std::shared_ptr<tablet::TabletPeer> tablet_peer;
      Status s = tablet_manager_->GetTabletPeer(checkpoint.tablet_id(), &tablet_peer);
      if (s.IsNotFound()) {
        continue;
      }
      // Don't create new tablet metrics if they have already been deleted.
      auto tablet_metric = GetCDCTabletMetrics(
          checkpoint.producer_tablet_info, tablet_peer, CreateCDCMetricsEntity::kFalse);
      if (!tablet_metric) {
        continue;
      }
      tablet_metric->ClearMetrics();
      RemoveCDCTabletMetrics(checkpoint.producer_tablet_info, tablet_peer);
    }
  }
}

bool CDCServiceImpl::ShouldUpdateLagMetrics(MonoTime time_since_update_metrics) {
  // Only update metrics if cdc is enabled, which means we have a valid replication stream.
  return GetAtomicFlag(&FLAGS_enable_collect_cdc_metrics) &&
         (time_since_update_metrics == MonoTime::kUninitialized ||
         MonoTime::Now() - time_since_update_metrics >=
             MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_update_metrics_interval_ms)));
}

bool CDCServiceImpl::CDCEnabled() {
  return cdc_enabled_.load(std::memory_order_acquire);
}

void CDCServiceImpl::SetCDCServiceEnabled() {
  cdc_enabled_.store(true, std::memory_order_release);
}

Result<std::shared_ptr<client::TableHandle>> CDCServiceImpl::GetCdcStateTable() {
  bool use_cache = GetAtomicFlag(&FLAGS_enable_cdc_state_table_caching);
  {
    SharedLock<decltype(mutex_)> l(mutex_);
    if (cdc_state_table_ && use_cache) {
      return cdc_state_table_;
    }
    if (cdc_service_stopped_) {
      return STATUS(ShutdownInProgress, "");
    }
  }

  auto cdc_state_table = std::make_shared<yb::client::TableHandle>();
  auto s = cdc_state_table->Open(kCdcStateTableName, client());
  // It is possible that this runs before the cdc_state table is created.
  RETURN_NOT_OK(s);

  {
    std::lock_guard<decltype(mutex_)> l(mutex_);
    if (cdc_state_table_ && use_cache) {
      return cdc_state_table_;
    }
    if (cdc_service_stopped_) {
      return STATUS(ShutdownInProgress, "");
    }
    cdc_state_table_ = cdc_state_table;
    return cdc_state_table_;
  }
}

  void CDCServiceImpl::RefreshCdcStateTable() {
    // Set cached value to null so we regenerate it on the next call.
    std::lock_guard<decltype(mutex_)> l(mutex_);
    cdc_state_table_ = nullptr;
  }

  Status CDCServiceImpl::RefreshCacheOnFail(const Status& s) {
    if (!s.ok()) {
      RefreshCdcStateTable();
    }
    return s;
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

  void PopulateTabletMinCheckpointAndLatestActiveTime(
      const string& tablet_id, const OpId& checkpoint, CDCRequestSource cdc_source_type,
      const int64_t& last_active_time, TabletIdCDCCheckpointMap* tablet_min_checkpoint_index) {
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
      tablet_info.cdc_sdk_latest_active_time =
          max(tablet_info.cdc_sdk_latest_active_time, last_active_time);
    }
  }

  Status CDCServiceImpl::SetInitialCheckPoint(
      const OpId& checkpoint, const string& tablet_id,
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer) {
    VLOG(1) << "Setting the checkpoint is " << checkpoint.ToString()
          << " and the latest entry OpID is " << tablet_peer->log()->GetLatestEntryOpId()
          << " for tablet_id: " << tablet_id;
    auto result = PopulateTabletCheckPointInfo(tablet_id);
    RETURN_NOT_OK_SET_CODE(result, CDCError(CDCErrorPB::INTERNAL_ERROR));
    TabletIdCDCCheckpointMap& tablet_min_checkpoint_map = *result;
    auto& tablet_op_id = tablet_min_checkpoint_map[tablet_id];
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
            tablet_op_id.cdc_sdk_op_id, tablet_op_id.cdc_sdk_op_id_expiration),
        CDCError(CDCErrorPB::INTERNAL_ERROR));

    //  Even if the flag is enable_update_local_peer_min_index is set, for the first time
    //  we need to set it to follower too.
    return UpdatePeersCdcMinReplicatedIndex(tablet_id, tablet_op_id);
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

    auto cdc_state_table_result = GetCdcStateTable();
    if (!cdc_state_table_result.ok()) {
      // It is possible that this runs before the cdc_state table is created. This is
      // ok. It just means that this is the first time the cluster starts.
      return STATUS_FORMAT(
          IllegalState, "Unable to open table $0. CDC min replicated indices won't be updated",
          kCdcStateTableName.table_name());
    }

    int count = 0;
    client::TableIteratorOptions options;
    Status failer_status;
    options.error_handler = [&failer_status](const Status& status) {
      LOG(WARNING) << "Scan of table " << kCdcStateTableName.table_name() << " failed: " << status;
      failer_status = status;
    };
    options.columns = std::vector<std::string>{
        master::kCdcTabletId, master::kCdcStreamId, master::kCdcCheckpoint,
        master::kCdcLastReplicationTime, master::kCdcData};

    for (const auto& row : client::TableRange(**cdc_state_table_result, options)) {
      count++;
      auto tablet_id = row.column(master::kCdcTabletIdIdx).string_value();
      auto stream_id = row.column(master::kCdcStreamIdIdx).string_value();
      auto checkpoint = row.column(master::kCdcCheckpointIdx).string_value();

      // Find the minimum checkpoint op_id per tablet. This minimum op_id
      // will be passed to LEADER and it's peers for log cache eviction and clean the consumed
      // intents in a regular interval.
      if (!input_tablet_id.empty() && input_tablet_id != tablet_id) {
        continue;
      }

      std::string last_replicated_time_str;
      const auto& timestamp_ql_value = row.column(3);
      if (!timestamp_ql_value.IsNull()) {
        last_replicated_time_str = timestamp_ql_value.timestamp_value().ToFormattedString();
      }

      int64_t last_active_time_cdc_state_table = std::numeric_limits<int64_t>::min();
      if (!row.column(4).IsNull()) {
        DCHECK_EQ(row.column(4).type(), InternalType::kMapValue);
        last_active_time_cdc_state_table = VERIFY_RESULT(
              CheckedStoInt<int64_t>(row.column(4).map_value().values(0).string_value()));
      }

      VLOG(1) << "stream_id: " << stream_id << ", tablet_id: " << tablet_id
            << ", checkpoint: " << checkpoint
            << ", last replicated time: " << last_replicated_time_str
            << ", last active time: " << last_active_time_cdc_state_table;

    // Add the {tablet_id, stream_id} pair to the set if its checkpoint is OpId::Max().
    if (tablet_stream_to_be_deleted && checkpoint == OpId::Max().ToString()) {
      tablet_stream_to_be_deleted->insert({tablet_id, stream_id});
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

    auto op_id_result = OpId::FromString(checkpoint);
    if (!op_id_result.ok()) {
      LOG(WARNING) << "Read invalid op id " << row.column(1).string_value() << " for tablet "
                   << tablet_id << ": " << op_id_result.status();
      continue;
    }
    const auto& op_id = *op_id_result;

      // If a tablet_id, stream_id pair is in "uninitialized state", we don't need to send the
      // checkpoint to the tablet peers.
      if (op_id == OpId::Invalid() && last_active_time_cdc_state_table == 0) {
        continue;
      }

      // Check that requested tablet_id is part of the CDC stream.
      ProducerTabletInfo producer_tablet = {"" /* UUID */, stream_id, tablet_id};

      // Check stream associated with the tablet is active or not.
      // Don't consider those inactive stream for the min_checkpoint calculation.
      int64_t latest_active_time = 0;
      if (record.source_type == CDCSDK) {
        // Support backward compatibility, where active_time as not part of cdc_state table.
        if (last_active_time_cdc_state_table == std::numeric_limits<int64_t>::min()) {
          LOG(WARNING)
              << "In previous server version, active time was not part of cdc_state table,"
               "as a part of upgrade, updating the active time forcefully for the tablet_id: "
              << tablet_id;
            last_active_time_cdc_state_table = GetCurrentTimeMicros();
        }
        auto session = client()->NewSession();
        auto status = CheckStreamActive(producer_tablet, session, last_active_time_cdc_state_table);
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
          }
          continue;
        }
        latest_active_time = last_active_time_cdc_state_table;
      }

      // Ignoring those non-bootstarped CDCSDK stream
      if (op_id != OpId::Invalid()) {
        PopulateTabletMinCheckpointAndLatestActiveTime(
            tablet_id, op_id, record.source_type, latest_active_time, &tablet_min_checkpoint_map);
      }
    }

    if (!failer_status.ok()) {
      RefreshCdcStateTable();
      return STATUS_FORMAT(
          IllegalState, "Failed to scan table $0: $1", kCdcStateTableName.table_name(),
          failer_status);
    }
    YB_LOG_EVERY_N_SECS(INFO, 300)
        << "Read " << count << " records from " << kCdcStateTableName.table_name();
    return tablet_min_checkpoint_map;
  }

  void CDCServiceImpl::UpdateTabletPeersWithMaxCheckpoint(
      const std::unordered_set<TabletId>& tablet_ids_with_max_checkpoint,
      std::unordered_set<TabletId>* failed_tablet_ids) {
    auto enable_update_local_peer_min_index =
        GetAtomicFlag(&FLAGS_enable_update_local_peer_min_index);

    TabletCDCCheckpointInfo tablet_info;
    tablet_info.cdc_sdk_op_id = OpId::Max();
    tablet_info.cdc_op_id = OpId::Max();
    tablet_info.cdc_sdk_latest_active_time = 0;

    for (const auto& tablet_id : tablet_ids_with_max_checkpoint) {
      auto s = UpdateTabletPeerWithCheckpoint(
          tablet_id, &tablet_info, enable_update_local_peer_min_index, false);

      if (!s.ok()) {
        failed_tablet_ids->insert(tablet_id);
        VLOG(1) << "Could not successfully update checkpoint as 'OpId::Max' for tablet: "
                << tablet_id << ", on all tablet peers";
      }
    }
  }

  void CDCServiceImpl::UpdateTabletPeersWithMinReplicatedIndex(
      TabletIdCDCCheckpointMap* tablet_min_checkpoint_map) {
    auto enable_update_local_peer_min_index =
        GetAtomicFlag(&FLAGS_enable_update_local_peer_min_index);

    for (auto& [tablet_id, tablet_info] : *tablet_min_checkpoint_map) {
      auto s = UpdateTabletPeerWithCheckpoint(
          tablet_id, &tablet_info, enable_update_local_peer_min_index);
    }
  }

  Status CDCServiceImpl::UpdateTabletPeerWithCheckpoint(
      const TabletId& tablet_id, TabletCDCCheckpointInfo* tablet_info,
      bool enable_update_local_peer_min_index, bool ignore_rpc_failures) {
    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    Status s = tablet_manager_->GetTabletPeer(tablet_id, &tablet_peer);
    if (s.IsNotFound()) {
      VLOG(2) << "Did not find tablet peer for tablet " << tablet_id;
      return STATUS_FORMAT(NotFound, "Tablet peer not found on current TServer");
    } else if (!enable_update_local_peer_min_index && !IsTabletPeerLeader(tablet_peer)) {
      VLOG(2) << "Tablet peer " << tablet_peer->permanent_uuid() << " is not the leader for tablet "
            << tablet_id;
      return STATUS_FORMAT(InternalError, "Current TServer does not host leader");
    }
    RETURN_NOT_OK(s);

    auto min_index = tablet_info->cdc_op_id.index;
    auto current_term = tablet_info->cdc_op_id.term;
    s = tablet_peer->set_cdc_min_replicated_index(min_index);
    WARN_NOT_OK(
        tablet_peer->set_cdc_min_replicated_index(min_index),
        "Unable to set cdc min index for tablet peer " + tablet_peer->permanent_uuid() +
            " and tablet: " + tablet_peer->tablet_id());
    RETURN_NOT_OK(s);

    auto result = tablet_peer->GetCDCSDKIntentRetainTime(tablet_info->cdc_sdk_latest_active_time);
    if (!result.ok()) {
      LOG(WARNING) << "Unable to get the intent retain time for tablet peer "
                 << tablet_peer->permanent_uuid() << " and tablet " << tablet_peer->tablet_id()
                 << ": " << s;
      return result.status();
    }
    tablet_info->cdc_sdk_op_id_expiration = *result;

    if (!enable_update_local_peer_min_index) {
      VLOG(1) << "Updating followers for tablet " << tablet_id << " with index " << min_index
              << " term " << current_term
              << " cdc_sdk_op_id: " << tablet_info->cdc_sdk_op_id.ToString()
              << " expiration: " << tablet_info->cdc_sdk_op_id_expiration.ToMilliseconds();
      s = UpdatePeersCdcMinReplicatedIndex(tablet_id, *tablet_info, ignore_rpc_failures);
      WARN_NOT_OK(s, "UpdatePeersCdcMinReplicatedIndex failed");
      if (!ignore_rpc_failures && !s.ok()) {
        return s;
      }
    } else {
      s = tablet_peer->SetCDCSDKRetainOpIdAndTime(
          tablet_info->cdc_sdk_op_id, tablet_info->cdc_sdk_op_id_expiration);
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
      const CDCStreamId stream_id,
      const std::set<TabletId>& active_or_hidden_tablets,
      const std::set<TabletId>& parent_tablets,
      const std::map<TabletId, TabletId>& child_to_parent_mapping,
      std::vector<std::pair<TabletId, OpId>>* result) {
    auto cdc_state_table_result = GetCdcStateTable();
    if (!cdc_state_table_result.ok()) {
      // It is possible that this runs before the cdc_state table is created. This is
      // ok. It just means that this is the first time the cluster starts.
      return STATUS_FORMAT(
          IllegalState,
          "Unable to open table $0, cannot proceed with GetTabletListToPollForCDC RPC",
          kCdcStateTableName.table_name());
    }

    client::TableIteratorOptions options;
    Status failer_status;
    options.error_handler = [&failer_status](const Status& status) {
      LOG(WARNING) << "Scan of table " << kCdcStateTableName.table_name() << " failed: " << status;
      failer_status = status;
    };
    options.columns = std::vector<std::string>{
        master::kCdcTabletId, master::kCdcStreamId, master::kCdcCheckpoint,
        master::kCdcLastReplicationTime};

    const auto& rows = client::TableRange(**cdc_state_table_result, options);
    std::set<TabletId> parents_with_polled_children;
    std::set<TabletId> polled_tablets;
    for (const auto& row : rows) {
      auto cur_stream_id = row.column(master::kCdcStreamIdIdx).string_value();
      if (cur_stream_id != stream_id) {
        continue;
      }

      auto tablet_id = row.column(master::kCdcTabletIdIdx).string_value();
      auto is_cur_tablet_polled = !row.column(master::kCdcLastReplicationTimeIdx - 1).IsNull();
      if (!is_cur_tablet_polled) {
        continue;
      }

      polled_tablets.insert(tablet_id);

      auto iter = child_to_parent_mapping.find(tablet_id);
      if (iter != child_to_parent_mapping.end()) {
        parents_with_polled_children.insert(iter->second);
      }
    }

    for (const auto& row : rows) {
      auto cur_stream_id = row.column(master::kCdcStreamIdIdx).string_value();
      if (cur_stream_id != stream_id) {
        continue;
      }

      auto tablet_id = row.column(master::kCdcTabletIdIdx).string_value();
      auto is_active_or_hidden =
          (active_or_hidden_tablets.find(tablet_id) != active_or_hidden_tablets.end());
      if (!is_active_or_hidden) {
        // This means the row is for a child tablet for which split is initiated but the process is
        // not complete.
        continue;
      }

      auto is_parent = (parent_tablets.find(tablet_id) != parent_tablets.end());
      auto checkpoint_result =
          OpId::FromString(row.column(master::kCdcCheckpointIdx).string_value());
      if (!checkpoint_result.ok()) {
        LOG(WARNING) << "Read invalid op id for tablet " << tablet_id << ": "
                    << checkpoint_result.status();
        continue;
      }
      auto checkpoint = checkpoint_result.get();
      auto is_cur_tablet_polled = !row.column(master::kCdcLastReplicationTimeIdx - 1).IsNull();

      bool add_to_result = false;
      auto parent_iter = child_to_parent_mapping.find(tablet_id);

      if (is_parent) {
        // This means the current tablet itself was a parent tablet. If we find
        // that we have already started polling the children, we will not add the parent tablet to
        // the result set. This situation is only possible within a small window where we have
        // reported the tablet split to the client and but the background thread has not yet deleted
        // the hidden parent tablet.
      bool is_any_child_polled =
          (parents_with_polled_children.find(tablet_id) != parents_with_polled_children.end());
      if (!is_any_child_polled) {
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
      // This means the current tablet is a child tablet, and not itself a parent tablet.
      if (checkpoint > OpId::Min() || is_cur_tablet_polled) {
        // This means the client has started polling on this child tablet already. So we will add
        // the current child tablet to the result set.
        add_to_result = true;
      } else {
        // This means the client has not started streaming from the child tablet. If we see that the
        // any ancestor tablet is also not polled we will add the current child tablet to the result
        // set.
        bool found_polled_ancestor = false;
        while (parent_iter != child_to_parent_mapping.end()) {
          const auto& ancestor_tablet_id = parent_iter->second;
          bool is_current_polled =
              (polled_tablets.find(ancestor_tablet_id) != polled_tablets.end());
          if (is_current_polled) {
            VLOG(1) << "Found polled ancestor tablet: " << ancestor_tablet_id
                    << ", for un-polled child tablet: " << tablet_id
                    << ". Hence this tablet is not yet ready to be polled by CDC stream: "
                    << stream_id;
            found_polled_ancestor = true;
            break;
          }

          // Get the iter to the parent of the current tablet.
          parent_iter = child_to_parent_mapping.find(ancestor_tablet_id);
        }

          if (!found_polled_ancestor) {
            add_to_result = true;
          }
        }
      }

      if (add_to_result) {
        result->push_back(std::make_pair(tablet_id, checkpoint));
      }
    }

    return Status::OK();
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
        // Have not yet received any GetChanges requests, so skip background thread work.
        continue;
      }
      // Should we update lag metrics default every 1s.
      if (ShouldUpdateLagMetrics(time_since_update_metrics)) {
        UpdateLagMetrics();
        time_since_update_metrics = MonoTime::Now();
      }

      // If its not been 60s since the last peer update, continue.
      if (!FLAGS_enable_log_retention_by_op_idx ||
          (time_since_update_peers != MonoTime::kUninitialized &&
          MonoTime::Now() - time_since_update_peers <
              MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_update_min_cdc_indices_interval_secs)))) {
        continue;
      }
      time_since_update_peers = MonoTime::Now();
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
    } while (sleep_while_not_stopped());
  }

  Status CDCServiceImpl::DeleteCDCStateTableMetadata(
      const TabletIdStreamIdSet& cdc_state_entries_to_delete,
      const std::unordered_set<TabletId>& failed_tablet_ids) {
    std::shared_ptr<yb::client::TableHandle> cdc_state_table_result =
        VERIFY_RESULT(GetCdcStateTable());
    auto session = client()->NewSession();
    if (!cdc_state_table_result) {
      return STATUS_FORMAT(
          IllegalState, "Unable to open table $0. CDC min replicated indices won't be updated",
          kCdcStateTableName.table_name());
    }

    // Iterating over set and deleting entries from the cdc_state table.
    for (const auto& [tablet_id, stream_id] : cdc_state_entries_to_delete) {
      if (failed_tablet_ids.contains(tablet_id)) {
        VLOG(2) << "We cannot delete the entry for the tablet: " << tablet_id
                << ", from cdc_state table yet. Since we encounterted failures while "
                   "propogating the checkpoint of OpId::Max to all the tablet peers";
        continue;
      }
      std::shared_ptr<tablet::TabletPeer> tablet_peer;
      Status s = tablet_manager_->GetTabletPeer(tablet_id, &tablet_peer);
      if (!s.ok()) {
        LOG(WARNING) << "Could not delete the entry for stream" << stream_id << " and the tablet "
                     << tablet_id;
        continue;
      }
      if (IsTabletPeerLeader(tablet_peer)) {
        const auto delete_op = cdc_state_table_result->NewDeleteOp();
        auto* const delete_req = delete_op->mutable_request();
        QLAddStringHashValue(delete_req, tablet_id);
        QLAddStringRangeValue(delete_req, stream_id);
        Status s = session->TEST_ApplyAndFlush(delete_op);
        if (!s.ok()) {
          LOG(WARNING) << "Unable to flush operations to delete cdc streams: " << s;
          return s.CloneAndPrepend("Error deleting cdc stream rows from cdc_state table");
        }
        LOG(INFO) << "CDC state table entry for tablet " << tablet_id << " and streamid "
                  << stream_id << " is deleted";
      }
    }
    return Status::OK();
  }

  Result<client::internal::RemoteTabletPtr> CDCServiceImpl::GetRemoteTablet(
      const TabletId& tablet_id, const bool use_cache) {
    std::promise<Result<client::internal::RemoteTabletPtr>> tablet_lookup_promise;
    auto future = tablet_lookup_promise.get_future();
    auto callback = [&tablet_lookup_promise](
      const Result<client::internal::RemoteTabletPtr>& result) {
    tablet_lookup_promise.set_value(result);
    };

    auto start = CoarseMonoClock::Now();
    client()->LookupTabletById(
        tablet_id,
        /* table =*/nullptr,
        // In case this is a split parent tablet, it will be hidden so we need this flag to access
        // it.
        master::IncludeInactive::kTrue,
        CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms),
        callback,
        GetAtomicFlag(&FLAGS_enable_cdc_client_tablet_caching) && use_cache
            ? client::UseCache::kTrue
            : client::UseCache::kFalse);
    future.wait();

    auto duration = CoarseMonoClock::Now() - start;
    if (duration > (kMaxDurationForTabletLookup * 1ms)) {
      LOG(WARNING) << "LookupTabletByKey took long time: " << duration << " ms";
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

  Status CDCServiceImpl::GetTServers(const TabletId& tablet_id,
                                   std::vector<client::internal::RemoteTabletServer*>* servers) {
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
      std::lock_guard<decltype(mutex_)> l(mutex_);
      auto it = cdc_service_map_.find(hostport);
      if (it != cdc_service_map_.end()) {
        return it->second;
      }
      cdc_service_map_.emplace(hostport, cdc_service);
    }
    return cdc_service;
  }

  void CDCServiceImpl::TabletLeaderGetChanges(const GetChangesRequestPB* req,
                                            GetChangesResponsePB* resp,
                                            std::shared_ptr<RpcContext> context,
                                            std::shared_ptr<tablet::TabletPeer> peer) {
    auto rpc_handle = rpcs_.Prepare();
    RPC_CHECK_AND_RETURN_ERROR(rpc_handle != rpcs_.InvalidHandle(),
        STATUS(Aborted,
            Format("Could not create valid handle for GetChangesCDCRpc: tablet=$0, peer=$1",
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

    *rpc_handle = CreateGetChangesCDCRpc(
      deadline,
      nullptr, /* RemoteTablet: will get this from 'new_req' */
      client(),
      &new_req,
      [this, resp, context, rpc_handle] (const Status& status, GetChangesResponsePB&& new_resp) {
        auto retained = rpcs_.Unregister(rpc_handle);
        *resp = std::move(new_resp);
        RPC_STATUS_RETURN_ERROR(status, resp->mutable_error(), resp->error().code(),
                                *context);
        context->RespondSuccess();
      });
    (**rpc_handle).SendRpc();
  }

  void CDCServiceImpl::TabletLeaderGetCheckpoint(const GetCheckpointRequestPB* req,
                                               GetCheckpointResponsePB* resp,
                                               RpcContext* context,
                                               const std::shared_ptr<tablet::TabletPeer>& peer) {
    auto result = GetLeaderTServer(req->tablet_id(), false /* use_cache */);
    RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
        CDCErrorPB::TABLET_NOT_FOUND, *context);

    auto ts_leader = *result;
    // Check that tablet leader identified by master is not current tablet peer.
    // This can happen during tablet rebalance if master and tserver have different views of
    // leader. We need to avoid self-looping in this case.
    if (peer) {
      RPC_CHECK_NE_AND_RETURN_ERROR(ts_leader->permanent_uuid(), peer->permanent_uuid(),
          STATUS(IllegalState,
              Format("Tablet leader changed: leader=$0, peer=$1",
                  ts_leader->permanent_uuid(),
                  peer->permanent_uuid())),
          resp->mutable_error(), CDCErrorPB::NOT_LEADER, *context);
    }

    auto cdc_proxy = GetCDCServiceProxy(ts_leader);
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));

    GetCheckpointRequestPB new_req;
    new_req.CopyFrom(*req);
    new_req.set_serve_as_proxy(false);

    // TODO(NIC): Change to GetCheckpointAsync like CDCPoller::DoPoll.
    auto status = cdc_proxy->GetCheckpoint(new_req, resp, &rpc);
    RPC_STATUS_RETURN_ERROR(status, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, *context);
    context->RespondSuccess();
  }

  void CDCServiceImpl::GetCheckpoint(const GetCheckpointRequestPB* req,
                                   GetCheckpointResponsePB* resp,
                                   RpcContext context) {
    if (!CheckOnline(req, resp, &context)) {
      return;
    }

    RPC_CHECK_AND_RETURN_ERROR(req->has_tablet_id(),
        STATUS(InvalidArgument, "Tablet ID is required to get CDC checkpoint"),
        resp->mutable_error(),
        CDCErrorPB::INVALID_REQUEST,
        context);
    RPC_CHECK_AND_RETURN_ERROR(req->has_stream_id(),
        STATUS(InvalidArgument, "Stream ID is required to get CDC checkpoint"),
        resp->mutable_error(),
        CDCErrorPB::INVALID_REQUEST,
        context);

    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    Status s = tablet_manager_->GetTabletPeer(req->tablet_id(), &tablet_peer);

    if ((s.IsNotFound() || !IsTabletPeerLeader(tablet_peer)) && req->serve_as_proxy()) {
      // Forward GetCheckpoint() to tablet leader. This happens often in Kubernetes setups.
      TabletLeaderGetCheckpoint(req, resp, &context, tablet_peer);
      return;
    } else if (!s.ok() || IsTabletPeerLeader(tablet_peer)) {
      VLOG(2) << "Current LEADER is not ready to serve tablet_id: " << req->tablet_id();
      RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::LEADER_NOT_READY, context);
    }

    // Check that requested tablet_id is part of the CDC stream.
    ProducerTabletInfo producer_tablet = {"" /* UUID */, req->stream_id(), req->tablet_id()};
    s = CheckTabletValidForStream(producer_tablet);
    RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

    auto session = client()->NewSession();
    CoarseTimePoint deadline = GetDeadline(context, client());

    session->SetDeadline(deadline);

    auto result = GetLastCheckpoint(producer_tablet, session);
    RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
        CDCErrorPB::INTERNAL_ERROR, context);

    result->ToPB(resp->mutable_checkpoint()->mutable_op_id());
    context.RespondSuccess();
  }

  void CDCServiceImpl::UpdateCdcReplicatedIndex(const UpdateCdcReplicatedIndexRequestPB* req,
                                              UpdateCdcReplicatedIndexResponsePB* resp,
                                              rpc::RpcContext context) {
    if (!CheckOnline(req, resp, &context)) {
      return;
    }

    // Backwards compatibility for deprecated fields.
    if (req->has_tablet_id() && req->has_replicated_index()) {
      boost::optional<int64> replicated_term;
      if (req->has_replicated_term()) {
        replicated_term = req->replicated_term();
      }
      Status s = UpdateCdcReplicatedIndexEntry(
          req->tablet_id(),
          req->replicated_index(),
          replicated_term,
          OpId::Max(),
          MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms)));
      RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);
      context.RespondSuccess();
      return;
    }

    RPC_CHECK_AND_RETURN_ERROR(req->tablet_ids_size() > 0 ||
                              req->replicated_indices_size() > 0 ||
                              req->replicated_terms_size() > 0,
                              STATUS(InvalidArgument, "Tablet ID, Index, & Term "
                                      "are all required to set the log replicated index"),
                              resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

    RPC_CHECK_AND_RETURN_ERROR(req->tablet_ids_size() == req->replicated_indices_size() &&
                              req->tablet_ids_size() == req->replicated_terms_size(),
                              STATUS(InvalidArgument, "Tablet ID, Index, & Term Count must match"),
                              resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

    // Todo: Add better failure handling? Modifications reverted by the caller right now.
    for (int i = 0; i < req->tablet_ids_size(); i++) {
      const OpId& cdc_sdk_op = req->cdc_sdk_consumed_ops().empty()
                                  ? OpId::Max()
                                  : OpId::FromPB(req->cdc_sdk_consumed_ops(i));
      const MonoDelta cdc_sdk_op_id_expiration = MonoDelta::FromMilliseconds(
          req->cdc_sdk_ops_expiration_ms().empty() ? GetAtomicFlag(&FLAGS_cdc_intent_retention_ms)
                                                  : req->cdc_sdk_ops_expiration_ms(i));
      Status s = UpdateCdcReplicatedIndexEntry(
          req->tablet_ids(i),
          req->replicated_indices(i),
          req->replicated_terms(i),
          cdc_sdk_op,
          cdc_sdk_op_id_expiration);
      RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);
    }
    context.RespondSuccess();
  }

  Status CDCServiceImpl::UpdateCdcReplicatedIndexEntry(
      const string& tablet_id, int64 replicated_index, boost::optional<int64> replicated_term,
      const OpId& cdc_sdk_replicated_op, const MonoDelta& cdc_sdk_op_id_expiration) {
    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    RETURN_NOT_OK(tablet_manager_->GetTabletPeer(tablet_id, &tablet_peer));
    if (!tablet_peer->log_available()) {
      return STATUS(TryAgain, "Tablet peer is not ready to set its log cdc index");
    }
    RETURN_NOT_OK(tablet_peer->set_cdc_min_replicated_index(replicated_index));
    RETURN_NOT_OK(
        tablet_peer->SetCDCSDKRetainOpIdAndTime(cdc_sdk_replicated_op, cdc_sdk_op_id_expiration));
    return Status::OK();
  }

  Result<OpId> CDCServiceImpl::TabletLeaderLatestEntryOpId(const TabletId& tablet_id) {
    auto ts_leader = VERIFY_RESULT(GetLeaderTServer(tablet_id));

    auto cdc_proxy = GetCDCServiceProxy(ts_leader);
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));
    GetLatestEntryOpIdRequestPB req;
    GetLatestEntryOpIdResponsePB resp;
    req.set_tablet_id(tablet_id);
    auto status = cdc_proxy->GetLatestEntryOpId(req, &resp, &rpc);
    if (!status.ok()) {
      // If we failed to get the latest entry op id, we try other tservers. The leader is guaranteed
      // to have the most up-to-date information, but for our purposes, it's ok to be slightly
      // behind.
      std::vector<client::internal::RemoteTabletServer *> servers;
      auto s = GetTServers(tablet_id, &servers);
      for (const auto& server : servers) {
        // We don't want to try the leader again.
        if (server->permanent_uuid() == ts_leader->permanent_uuid()) {
          continue;
        }
        auto follower_cdc_proxy = GetCDCServiceProxy(server);
        status = follower_cdc_proxy->GetLatestEntryOpId(req, &resp, &rpc);
        if (status.ok()) {
          return OpId::FromPB(resp.op_id());
        }
      }
      DCHECK(!status.ok());
      return status;
    }
    return OpId::FromPB(resp.op_id());
  }

// Given a list of tablet ids, retrieve the latest entry op_id for each of them.
// The response should contain a list of op_ids for each input tablet id that was
// successfully processed, in the same order that the tablet ids were passed in.
void CDCServiceImpl::GetLatestEntryOpId(const GetLatestEntryOpIdRequestPB* req,
                                        GetLatestEntryOpIdResponsePB* resp,
                                        rpc::RpcContext context) {
  std::shared_ptr<tablet::TabletPeer> tablet_peer;

  // Support backwards compatibility.
  if (req->has_tablet_id()) {
    Status s = tablet_manager_->GetTabletPeer(req->tablet_id(), &tablet_peer);
    RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

    if (!tablet_peer->log_available()) {
      const string err_message = strings::Substitute("Unable to get the latest entry op id from "
          "peer $0 and tablet $1 because its log object hasn't been initialized",
          tablet_peer->permanent_uuid(), tablet_peer->tablet_id());
      LOG(WARNING) << err_message;
      SetupErrorAndRespond(resp->mutable_error(), STATUS(ServiceUnavailable, err_message),
                           CDCErrorPB::INTERNAL_ERROR, &context);
      return;
    }
    OpId op_id = tablet_peer->log()->GetLatestEntryOpId();
    op_id.ToPB(resp->mutable_op_id());

    context.RespondSuccess();
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(req->tablet_ids_size() > 0, STATUS(InvalidArgument,
                               "Tablet IDs are required to set the log replicated index"),
                             resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  for (int i = 0; i < req->tablet_ids_size(); i++) {
    Status s = tablet_manager_->GetTabletPeer(req->tablet_ids(i), &tablet_peer);
    RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

    if (!tablet_peer->log_available()) {
      const string err_message = strings::Substitute("Unable to get the latest entry op id from "
          "peer $0 and tablet $1 because its log object hasn't been initialized",
          tablet_peer->permanent_uuid(), tablet_peer->tablet_id());
      LOG(WARNING) << err_message;
      SetupErrorAndRespond(resp->mutable_error(), STATUS(ServiceUnavailable, err_message),
                          CDCErrorPB::INTERNAL_ERROR, &context);
      return;
    }

    // Add op_id to response.
    OpId op_id = tablet_peer->log()->GetLatestEntryOpId();
    op_id.ToPB(resp->add_op_ids());
  }

  context.RespondSuccess();
}

void CDCServiceImpl::GetCDCDBStreamInfo(const GetCDCDBStreamInfoRequestPB* req,
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
    WARN_NOT_OK(client()->DeleteCDCStream(creation_state.created_cdc_streams),
                "Unable to delete streams " + JoinCSVLine(creation_state.created_cdc_streams));
  }

  // For all tablets we modified state for, reverse those changes if the operation failed
  // halfway through.
  if (creation_state.producer_entries_modified.empty()) {
    return;
  }
  {
    std::lock_guard<decltype(mutex_)> l(mutex_);
    impl_->EraseTablets(creation_state.producer_entries_modified, false);
  }
  for (const auto& entry : creation_state.producer_entries_modified) {
    // Update the term and index for the consumed checkpoint to tablet's LEADER as well as FOLLOWER.
    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    Status s = tablet_manager_->GetTabletPeer(entry.tablet_id, &tablet_peer);
    if (s.ok()) { // if local
      WARN_NOT_OK(tablet_peer->set_cdc_min_replicated_index(kOpIdMax.cdc_op_id.index),
                  "Unable to update min index for local tablet " + entry.tablet_id);
    }
    WARN_NOT_OK(UpdatePeersCdcMinReplicatedIndex(entry.tablet_id, kOpIdMax),
                "Unable to update min index for remote tablet " + entry.tablet_id);
  }
}

void CDCServiceImpl::XClusterAsyncPromiseCallback(std::promise<void>* const promise,
                                                  std::atomic<int>* const finished_tasks,
                                                  int total_tasks) {
  // If this is the last of the tasks to finish, then mark the promise as fulfilled.
  if (++(*finished_tasks) == total_tasks) {
    promise->set_value();
  }
}

void CDCServiceImpl::BootstrapProducer(const BootstrapProducerRequestPB* req,
                                       BootstrapProducerResponsePB* resp,
                                       rpc::RpcContext context) {
  LOG(INFO) << "Received BootstrapProducer request " << req->ShortDebugString();
  RPC_CHECK_AND_RETURN_ERROR(req->table_ids().size() > 0,
                             STATUS(InvalidArgument, "Table ID is required to create CDC stream"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  std::shared_ptr<client::TableHandle> cdc_state_table;

  std::vector<client::YBOperationPtr> ops;
  auto session = client()->NewSession();

  // Used to delete streams in case of failure.
  CDCCreationState creation_state;
  auto scope_exit = ScopeExit([this, &creation_state] {
    RollbackPartialCreate(creation_state);
  });

  // Decide which version of bootstrap producer to use.
  Status s;
  if (PREDICT_TRUE(FLAGS_parallelize_bootstrap_producer)) {
    s = BootstrapProducerHelperParallelized(req, resp, &ops, &creation_state);
  } else {
    s = BootstrapProducerHelper(req, resp, &ops, &creation_state);
  }

  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  // On a success, apply cdc state table ops.
  session->SetDeadline(GetDeadline(context, client()));
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  s = RefreshCacheOnFail(session->TEST_ApplyAndFlush(ops));
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  // Clear these vectors so no changes are reversed by scope_exit since we succeeded.
  creation_state.Clear();
  context.RespondSuccess();
}

// Type definitions specific to BootstrapProducerHelperParallelized.
typedef std::pair<std::string, std::string> BootstrapTabletPair;

// BootstrapProducerHelperParallelized tries to optimize the throughput of this operation. It runs
// tablet operations in parallel & batching to reduce overall RPC count. Steps:
// 1. Create CDC Streams for each Table under Bootstrap
// 2. Create a server : list(tablet) mapping for these Tables
// 3. Async per server, get the Latest OpID on each tablet leader.
// 4. Async per server, Set WAL Retention on each tablet peer. This is the most expensive operation.
Status CDCServiceImpl::BootstrapProducerHelperParallelized(
  const BootstrapProducerRequestPB* req,
  BootstrapProducerResponsePB* resp,
  std::vector<client::YBOperationPtr>* ops,
  CDCCreationState* creation_state) {

  std::vector<CDCStreamId> bootstrap_ids;
  // For each (bootstrap_id, tablet_id) pair, store its op_id object.
  std::unordered_map<BootstrapTabletPair, yb::OpId, boost::hash<BootstrapTabletPair>> tablet_op_ids;
  // For each server id, store the server proxy object.
  std::unordered_map<std::string, std::shared_ptr<CDCServiceProxy>> server_to_proxy;
  // For each server, store tablets that we need to make an rpc call to that server with.
  std::unordered_map<std::string, std::vector<BootstrapTabletPair>> server_to_remote_tablets;
  std::unordered_map<std::string, std::vector<BootstrapTabletPair>> server_to_remote_tablet_leader;

  LOG_WITH_FUNC(INFO) << "Initializing CDC Streams";
  for (const auto& table_id : req->table_ids()) {
    std::shared_ptr<client::YBTable> table;
    RETURN_NOT_OK(client()->OpenTable(table_id, &table));

    // 1. Generate a bootstrap id & setup the CDC stream, for use with the XCluster Consumer.
    std::unordered_map<std::string, std::string> options;
    options.reserve(2);
    options.emplace(cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));
    options.emplace(cdc::kRecordFormat, CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));

    // Mark this stream as being bootstrapped, to help in finding dangling streams.
    // TODO: Turn this into a batch RPC.
    const std::string& bootstrap_id = VERIFY_RESULT(
      client()->CreateCDCStream(table_id, options, false));
    creation_state->created_cdc_streams.push_back(bootstrap_id);

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(client()->GetTabletsFromTableId(table_id, 0, &tablets));

    // 2. Create a server : list(tablet) mapping for these Tables
    for (const auto& tablet : tablets) {
      const std::string tablet_id = tablet.tablet_id();
      auto bootstrap_tablet_pair = std::make_pair(bootstrap_id, tablet_id);
      std::shared_ptr<tablet::TabletPeer> tablet_peer;
      OpId op_id = yb::OpId(-1, -1);

      // Get remote servers for tablet.
      std::vector<client::internal::RemoteTabletServer *> servers;
      RETURN_NOT_OK(GetTServers(tablet_id, &servers));

      // Check if this tablet has local information cached.
      Status s = tablet_manager_->GetTabletPeer(tablet_id, &tablet_peer);
      if (s.ok()) {
        // Retrieve op_id from local cache.
        if (!tablet_peer->log_available()) {
          const string err_message = strings::Substitute("Unable to get the latest entry op id "
              "from peer $0 and tablet $1 because its log object hasn't been initialized",
              tablet_peer->permanent_uuid(), tablet_id);
          LOG(WARNING) << err_message;
          return STATUS(InternalError, err_message);
        }
        op_id = tablet_peer->log()->GetLatestEntryOpId();

        // Add checkpoint for rollback before modifying tablet state.
        impl_->AddTabletCheckpoint(op_id, bootstrap_id, tablet_id,
                                   &creation_state->producer_entries_modified);

        // All operations local tablets can be done now.
        RETURN_NOT_OK(tablet_peer->set_cdc_min_replicated_index(op_id.index));
        RETURN_NOT_OK(DoUpdateCDCConsumerOpId(tablet_peer, op_id, tablet_id));

        // Store remote tablet information so we can do batched rpc calls.
        for (const auto& server : servers) {
          // We modify our log directly. Avoid calling itself through the proxy.
          if (server->IsLocal()) {
            continue;
          }

          const std::string server_id = server->permanent_uuid();

          // Save server_id to proxy mapping.
          if (server_to_proxy.count(server_id) == 0) {
            server_to_proxy[server_id] = GetCDCServiceProxy(server);
          }

          // Add tablet to the tablet list for this server
          server_to_remote_tablets[server_id].push_back(bootstrap_tablet_pair);
        }
      } else { // Not local.
        // Fetch and store the leader tserver so we can get opids from it later.
        auto ts_leader = VERIFY_RESULT(GetLeaderTServer(tablet_id));
        const std::string leader_server_id = ts_leader->permanent_uuid();

        // Add mapping from server to tablet leader.
        server_to_remote_tablet_leader[leader_server_id].push_back(bootstrap_tablet_pair);

        // Add mapping from leader server to proxy.
        if (server_to_proxy.count(leader_server_id) == 0) {
          server_to_proxy[leader_server_id] = GetCDCServiceProxy(ts_leader);
        }
      }

      // Add (bootstrap_id, tablet_id) to op_id entry
      tablet_op_ids[bootstrap_tablet_pair] = std::move(op_id);
    }
    bootstrap_ids.push_back(std::move(bootstrap_id));
  }

  LOG_WITH_FUNC(INFO) << "Retrieving Latest OpIDs for each tablet.";
  // Stores number of async rpc calls that have returned.
  std::atomic<int> finished_tasks{0};
  // Promise used to wait for rpc calls to all complete.
  std::promise<void> get_op_id_promise;
  auto get_op_id_future = get_op_id_promise.get_future();
  // Store references to the rpc and response objects so they don't go out of scope.
  std::vector<std::shared_ptr<rpc::RpcController>> rpcs;
  std::unordered_map<std::string, std::shared_ptr<GetLatestEntryOpIdResponsePB>>
    get_op_id_responses_by_server;

  // 3. Async per server, get the Latest OpID on each tablet leader.
  for (const auto& server_tablet_list_pair : server_to_remote_tablet_leader) {
    auto rpc = std::make_shared<rpc::RpcController>();
    rpcs.push_back(rpc);

    // Add pointers to rpc and response objects to respective in memory data structures.
    GetLatestEntryOpIdRequestPB get_op_id_req;
    for (auto& bootstrap_id_tablet_id_pair : server_tablet_list_pair.second) {
      get_op_id_req.add_tablet_ids(bootstrap_id_tablet_id_pair.second);
    }
    auto get_op_id_resp = std::make_shared<GetLatestEntryOpIdResponsePB>();
    get_op_id_responses_by_server[server_tablet_list_pair.first] = get_op_id_resp;

    auto proxy = server_to_proxy[server_tablet_list_pair.first];
    // Todo: GetLatestEntryOpId does not seem to enforce this deadline.
    rpc.get()->set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    proxy->GetLatestEntryOpIdAsync(get_op_id_req, get_op_id_resp.get(), rpc.get(),
      std::bind(&CDCServiceImpl::XClusterAsyncPromiseCallback, this, &get_op_id_promise,
                &finished_tasks, server_to_remote_tablet_leader.size())
    );
  }

  // Wait for all async rpc calls to finish.
  if (server_to_remote_tablet_leader.size() > 0) {
    get_op_id_future.wait();
  }

  // Parse responses and update producer_entries_modified and tablet_checkpoints_.
  std::string get_op_id_err_message;
  for (const auto& server_id_resp_pair : get_op_id_responses_by_server) {
    const std::string server_id = server_id_resp_pair.first;
    const auto get_op_id_resp = server_id_resp_pair.second.get();
    const auto leader_tablets = server_to_remote_tablet_leader[server_id];

    // Record which tablets we retrieved an op id from & record in local cache.
    for (int i = 0; i < get_op_id_resp->op_ids_size(); i++) {
      const std::string bootstrap_id = leader_tablets.at(i).first;
      const std::string tablet_id = leader_tablets.at(i).second;
      ProducerTabletInfo producer_tablet{
        "" /* Universe UUID */, bootstrap_id, tablet_id};
      auto op_id = OpId::FromPB(get_op_id_resp->op_ids(i));

      // Add op_id for tablet.
      tablet_op_ids[std::make_pair(bootstrap_id, tablet_id)] = std::move(op_id);

      // Add checkpoint for rollback before modifying tablet state.
      impl_->AddTabletCheckpoint(op_id, bootstrap_id, tablet_id,
                                 &creation_state->producer_entries_modified);
    }

    // Note any errors, but continue processing all RPC results.
    if (get_op_id_resp->has_error()) {
      auto err_message = get_op_id_resp->error().status().message();
      LOG(WARNING) << "Error from " << server_id << ": " << err_message;
      if (get_op_id_err_message.empty()) {
        get_op_id_err_message = err_message;
      }
    }
  }

  // Return if there is an error.
  if (!get_op_id_err_message.empty()) {
    return STATUS(InternalError, get_op_id_err_message);
  }

  // Check that all tablets have a valid op id.
  for (const auto& tablet_op_id_pair : tablet_op_ids) {
    if (!tablet_op_id_pair.second.valid()) {
      return STATUS(InternalError, "Could not retrieve op id for tablet",
                                   tablet_op_id_pair.first.second);
    }
  }

  LOG_WITH_FUNC(INFO) << "Updating OpIDs for Log Retention.";
  std::promise<void> update_index_promise;
  auto update_index_future = update_index_promise.get_future();
  // Reuse finished_tasks and rpc vector from before.
  finished_tasks = 0;
  rpcs.clear();
  std::vector<std::shared_ptr<UpdateCdcReplicatedIndexResponsePB>> update_index_responses;

  // 4. Async per server, Set WAL Retention on each tablet peer.
  for (const auto& server_tablet_list_pair : server_to_remote_tablets) {
    UpdateCdcReplicatedIndexRequestPB update_index_req;
    auto update_index_resp = std::make_shared<UpdateCdcReplicatedIndexResponsePB>();
    auto rpc = std::make_shared<rpc::RpcController>();

    // Store pointers to response and rpc object.
    update_index_responses.push_back(update_index_resp);
    rpcs.push_back(rpc);

    for (auto& bootstrap_id_tablet_id_pair : server_tablet_list_pair.second) {
      update_index_req.add_tablet_ids(bootstrap_id_tablet_id_pair.second);
      update_index_req.add_replicated_indices(tablet_op_ids[bootstrap_id_tablet_id_pair].index);
      update_index_req.add_replicated_terms(tablet_op_ids[bootstrap_id_tablet_id_pair].term);
    }

    auto proxy = server_to_proxy[server_tablet_list_pair.first];
    // Todo: UpdateCdcReplicatedIndex does not seem to enforce this deadline.
    rpc.get()->set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    proxy->UpdateCdcReplicatedIndexAsync(update_index_req, update_index_resp.get(), rpc.get(),
      std::bind(&CDCServiceImpl::XClusterAsyncPromiseCallback, this, &update_index_promise,
                &finished_tasks, server_to_remote_tablets.size()));
  }

  // Wait for all async calls to finish.
  if (server_to_remote_tablets.size() > 0) {
    update_index_future.wait();
  }

  // Check all responses for errors.
  for (const auto& update_index_resp : update_index_responses) {
    if (update_index_resp->has_error()) {
      const string err_message = update_index_resp->error().status().message();;
      LOG(WARNING) << err_message;
      return STATUS(InternalError, err_message);
    }
  }

  std::shared_ptr<yb::client::TableHandle> cdc_state_table = VERIFY_RESULT(GetCdcStateTable());

  // Create CDC state table update ops with all bootstrap id to tablet id pairs.
  for (const auto& bootstrap_id_tablet_id_to_op_id_pair : tablet_op_ids) {
    auto bootstrap_id_tablet_id_pair = bootstrap_id_tablet_id_to_op_id_pair.first;
    auto op_id = bootstrap_id_tablet_id_to_op_id_pair.second;

    const auto op = cdc_state_table->NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto *const write_req = op->mutable_request();

    // Add tablet id.
    QLAddStringHashValue(write_req, bootstrap_id_tablet_id_pair.second);
    // Add bootstrap id.
    QLAddStringRangeValue(write_req, bootstrap_id_tablet_id_pair.first);
    cdc_state_table->AddStringColumnValue(write_req, master::kCdcCheckpoint, op_id.ToString());
    ops->push_back(std::move(op));
  }

  // Update response with bootstrap ids.
  for (const auto& bootstrap_id : bootstrap_ids) {
    resp->add_cdc_bootstrap_ids(bootstrap_id);
  }
  LOG_WITH_FUNC(INFO) << "Finished.";

  return Status::OK();
}

Status CDCServiceImpl::BootstrapProducerHelper(
  const BootstrapProducerRequestPB* req,
  BootstrapProducerResponsePB* resp,
  std::vector<client::YBOperationPtr>* ops,
  CDCCreationState* creation_state) {

  std::shared_ptr<yb::client::TableHandle> cdc_state_table;
  std::vector<CDCStreamId> bootstrap_ids;

  for (const auto& table_id : req->table_ids()) {
    std::shared_ptr<client::YBTable> table;
    RETURN_NOT_OK(client()->OpenTable(table_id, &table));

    // Generate a bootstrap id by calling CreateCDCStream, and also setup the stream in the master.
    // If the consumer's master sends a CreateCDCStream with a bootstrap id, the producer's master
    // will verify that the stream id exists and return success if it does since everything else
    // has already been done by this call.
    std::unordered_map<std::string, std::string> options;
    options.reserve(4);
    options.emplace(cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));
    options.emplace(cdc::kRecordFormat, CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));
    options.emplace(cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::XCLUSTER));
    options.emplace(cdc::kCheckpointType, CDCCheckpointType_Name(cdc::CDCCheckpointType::IMPLICIT));

    // Mark this stream as being bootstrapped, to help in finding dangling streams.
    const std::string& bootstrap_id = VERIFY_RESULT(
      client()->CreateCDCStream(table_id, options, /* active */ false));
    creation_state->created_cdc_streams.push_back(bootstrap_id);

    if (cdc_state_table == nullptr) {
      cdc_state_table = VERIFY_RESULT(GetCdcStateTable());
    }

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(client()->GetTabletsFromTableId(table_id, 0, &tablets));

    // For each tablet, create a row in cdc_state table containing the generated bootstrap id, and
    // the latest op id in the logs.
    for (const auto &tablet : tablets) {
      std::shared_ptr<tablet::TabletPeer> tablet_peer;
      OpId op_id;

      Status s = tablet_manager_->GetTabletPeer(tablet.tablet_id(), &tablet_peer);
      // Get the Latest OpID.
      TabletCDCCheckpointInfo op_id_min;
      if (s.ok()) {
        if (!tablet_peer->log_available()) {
          const string err_message = strings::Substitute("Unable to get the latest entry op id "
              "from peer $0 and tablet $1 because its log object hasn't been initialized",
              tablet_peer->permanent_uuid(), tablet_peer->tablet_id());
          LOG(WARNING) << err_message;
          return STATUS(InternalError, err_message);
        }
        op_id = tablet_peer->log()->GetLatestEntryOpId();
        // Update the term and index for the consumed checkpoint
        // to tablet's LEADER as well as FOLLOWER.
        op_id_min.cdc_op_id = OpId(OpId::kUnknownTerm, op_id.index);
        op_id_min.cdc_sdk_op_id = OpId::Max();

        RETURN_NOT_OK(tablet_peer->set_cdc_min_replicated_index(op_id.index));
      } else { // Remote tablet.
        op_id = VERIFY_RESULT(TabletLeaderLatestEntryOpId(tablet.tablet_id()));
        op_id_min.cdc_op_id = OpId(OpId::kUnknownTerm, op_id.index);
        op_id_min.cdc_sdk_op_id = OpId::Max();
      }
      // Even though we let each log independently take care of updating its own log checkpoint,
      // we still call the Update RPC when we create the replication stream.
      RETURN_NOT_OK(UpdatePeersCdcMinReplicatedIndex(tablet.tablet_id(), op_id_min));

      const auto op = cdc_state_table->NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
      auto *const write_req = op->mutable_request();

      QLAddStringHashValue(write_req, tablet.tablet_id());
      QLAddStringRangeValue(write_req, bootstrap_id);
      cdc_state_table->AddStringColumnValue(write_req, master::kCdcCheckpoint, op_id.ToString());
      ops->push_back(std::move(op));
      impl_->AddTabletCheckpoint(
          op_id, bootstrap_id, tablet.tablet_id(), &creation_state->producer_entries_modified);
    }
    bootstrap_ids.push_back(std::move(bootstrap_id));
  }

  // Add bootstrap ids to response.
  for (const auto& bootstrap_id : bootstrap_ids) {
    resp->add_cdc_bootstrap_ids(bootstrap_id);
  }

  return Status::OK();
}

void CDCServiceImpl::Shutdown() {
  if (impl_->async_client_init_) {
    impl_->async_client_init_->Shutdown();
    rpcs_.Shutdown();
    {
      std::lock_guard<decltype(mutex_)> l(mutex_);
      cdc_service_stopped_ = true;
      cdc_state_table_ = nullptr;
    }
    if (update_peers_and_metrics_thread_) {
      update_peers_and_metrics_thread_->Join();
    }
    impl_->async_client_init_ = boost::none;
    impl_->ClearCaches();
  }
}

Status CDCServiceImpl::CheckStreamActive(
    const ProducerTabletInfo& producer_tablet, const client::YBSessionPtr& session,
    const int64_t& last_active_time_passed) {
  auto last_active_time = (last_active_time_passed == 0)
                              ? VERIFY_RESULT(GetLastActiveTime(producer_tablet, session))
                              : last_active_time_passed;

  auto now = GetCurrentTimeMicros();
  if (now < last_active_time + 1000 * (GetAtomicFlag(&FLAGS_cdc_intent_retention_ms))) {
    VLOG(1) << "Tablet: " << producer_tablet.ToString()
            << " found in CDCState table/ cache with active time: " << last_active_time
            << " current time:" << now << ", for stream: " << producer_tablet.stream_id;
    return Status::OK();
  }

  last_active_time = VERIFY_RESULT(GetLastActiveTime(producer_tablet, session, true));
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
    const ProducerTabletInfo& producer_tablet, const client::YBSessionPtr& session,
    bool ignore_cache) {
  DCHECK(!producer_tablet.stream_id.empty() && !producer_tablet.tablet_id.empty());

  if (!ignore_cache) {
    auto result = impl_->GetLastActiveTime(producer_tablet);
    if (result) {
      return *result;
    }
  }

  auto cdc_state_table_result = GetCdcStateTable();
  RETURN_NOT_OK(cdc_state_table_result);

  const auto readop = (*cdc_state_table_result)->NewReadOp();
  auto* const readreq = readop->mutable_request();
  QLAddStringHashValue(readreq, producer_tablet.tablet_id);

  auto cond = readreq->mutable_where_expr()->mutable_condition();
  cond->set_op(QLOperator::QL_OP_AND);
  QLAddStringCondition(
      cond, Schema::first_column_id() + master::kCdcStreamIdIdx, QL_OP_EQUAL,
      producer_tablet.stream_id);
  readreq->mutable_column_refs()->add_ids(Schema::first_column_id() + master::kCdcTabletIdIdx);
  readreq->mutable_column_refs()->add_ids(Schema::first_column_id() + master::kCdcStreamIdIdx);
  (*cdc_state_table_result)->AddColumns({master::kCdcData}, readreq);

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK(session->TEST_ReadSync(readop));
  auto row_block = ql::RowsResult(readop.get()).GetRowBlock();

  if (row_block->row_count() != 1) {
    // This could happen when conncurently as this function is running the stram is deleted, in
    // which case we return last active_time as "0".
    return 0;
  }
  if (!row_block->row(0).column(0).IsNull()) {
    DCHECK_EQ(row_block->row(0).column(0).type(), InternalType::kMapValue);
    if (row_block->row(0).column(0).map_value().values().size() == 1) {
      auto last_active_time_string =
          row_block->row(0).column(0).map_value().values().Get(0).string_value();
      auto last_active_time = VERIFY_RESULT(CheckedStoInt<int64_t>(last_active_time_string));

      VLOG(2) << "Found entry in cdc_state table with active time: " << last_active_time
              << ", for tablet: " << producer_tablet.tablet_id
              << ", and stream: " << producer_tablet.stream_id;

      return last_active_time;
    }
  }

  return GetCurrentTimeMicros();
}

Result<OpId> CDCServiceImpl::GetLastCheckpoint(
    const ProducerTabletInfo& producer_tablet,
    const client::YBSessionPtr& session) {
  auto result = impl_->GetLastCheckpoint(producer_tablet);
  if (result) {
    return *result;
  }

  auto cdc_state_table_result = GetCdcStateTable();
  RETURN_NOT_OK(cdc_state_table_result);

  const auto op = (*cdc_state_table_result)->NewReadOp();
  auto* const req = op->mutable_request();
  DCHECK(!producer_tablet.stream_id.empty() && !producer_tablet.tablet_id.empty());
  QLAddStringHashValue(req, producer_tablet.tablet_id);

  auto cond = req->mutable_where_expr()->mutable_condition();
  cond->set_op(QLOperator::QL_OP_AND);
  QLAddStringCondition(cond, Schema::first_column_id() + master::kCdcStreamIdIdx,
      QL_OP_EQUAL, producer_tablet.stream_id);
  req->mutable_column_refs()->add_ids(Schema::first_column_id() + master::kCdcTabletIdIdx);
  req->mutable_column_refs()->add_ids(Schema::first_column_id() + master::kCdcStreamIdIdx);
  (*cdc_state_table_result)->AddColumns({master::kCdcCheckpoint}, req);

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK(RefreshCacheOnFail(session->TEST_ReadSync(op)));
  auto row_block = ql::RowsResult(op.get()).GetRowBlock();
  if (row_block->row_count() == 0) {
    LOG(WARNING) << "Did not find any row in the cdc state table for tablet: "
                 << producer_tablet.tablet_id << ", stream " << producer_tablet.stream_id;
    return OpId(0, 0);
  }

  DCHECK_EQ(row_block->row_count(), 1);
  DCHECK_EQ(row_block->row(0).column(0).type(), InternalType::kStringValue);

  return OpId::FromString(row_block->row(0).column(0).string_value());
}

void CDCServiceImpl::UpdateCDCTabletMetrics(
    const GetChangesResponsePB* resp,
    const ProducerTabletInfo& producer_tablet,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const OpId& op_id,
    int64_t last_readable_index) {
  auto tablet_metric = GetCDCTabletMetrics(producer_tablet, tablet_peer);
  if (!tablet_metric) {
    return;
  }

  auto lid = resp->checkpoint().op_id();
  tablet_metric->last_read_opid_term->set_value(lid.term());
  tablet_metric->last_read_opid_index->set_value(lid.index());
  tablet_metric->last_readable_opid_index->set_value(last_readable_index);
  tablet_metric->last_checkpoint_opid_index->set_value(op_id.index);
  if (resp->records_size() > 0) {
    auto& last_record = resp->records(resp->records_size() - 1);
    tablet_metric->last_read_hybridtime->set_value(last_record.time());
    auto last_record_micros = HybridTime(last_record.time()).GetPhysicalValueMicros();
    tablet_metric->last_read_physicaltime->set_value(last_record_micros);
    // Only count bytes responded if we are including a response payload.
    tablet_metric->rpc_payload_bytes_responded->Increment(resp->ByteSize());
    // Get the physical time of the last committed record on producer.
    auto last_replicated_micros = GetLastReplicatedTime(tablet_peer);
    tablet_metric->async_replication_sent_lag_micros->set_value(
        last_replicated_micros - last_record_micros);
    auto& first_record = resp->records(0);
    auto first_record_micros = HybridTime(first_record.time()).GetPhysicalValueMicros();
    tablet_metric->last_checkpoint_physicaltime->set_value(first_record_micros);
    tablet_metric->async_replication_committed_lag_micros->set_value(
        last_replicated_micros - first_record_micros);
  } else {
    tablet_metric->rpc_heartbeats_responded->Increment();
    // If there are no more entries to be read, that means we're caught up.
    auto last_replicated_micros = GetLastReplicatedTime(tablet_peer);
    tablet_metric->last_read_physicaltime->set_value(last_replicated_micros);
    tablet_metric->last_checkpoint_physicaltime->set_value(last_replicated_micros);
    tablet_metric->async_replication_sent_lag_micros->set_value(0);
    tablet_metric->async_replication_committed_lag_micros->set_value(0);
  }
}

Status CDCServiceImpl::UpdateCheckpointAndActiveTime(
    const ProducerTabletInfo& producer_tablet,
    const OpId& sent_op_id,
    const OpId& commit_op_id,
    const client::YBSessionPtr& session,
    uint64_t last_record_hybrid_time,
    const CDCRequestSource& request_source,
    const bool force_update) {
  bool update_cdc_state = impl_->UpdateCheckpoint(producer_tablet, sent_op_id, commit_op_id);
  if (update_cdc_state || force_update) {
    auto cdc_state = VERIFY_RESULT(GetCdcStateTable());
    const auto op = cdc_state->NewUpdateOp();
    auto* const req = op->mutable_request();
    DCHECK(!producer_tablet.stream_id.empty() && !producer_tablet.tablet_id.empty());
    QLAddStringHashValue(req, producer_tablet.tablet_id);
    QLAddStringRangeValue(req, producer_tablet.stream_id);

    cdc_state->AddStringColumnValue(req, master::kCdcCheckpoint, commit_op_id.ToString());
    // If we have a last record hybrid time, use that for physical time. If not, it means we're
    // caught up, so the current time.
    uint64_t last_replication_time_micros = last_record_hybrid_time != 0 ?
        HybridTime(last_record_hybrid_time).GetPhysicalValueMicros() : GetCurrentTimeMicros();
    cdc_state->AddTimestampColumnValue(
        req, master::kCdcLastReplicationTime, last_replication_time_micros);

    if (request_source == CDCSDK) {
      auto last_active_time = GetCurrentTimeMicros();
      auto column_id = cdc_state->ColumnId(master::kCdcData);
      cdc_state->AddMapColumnValue(req, column_id, "active_time", ToString(last_active_time));

      VLOG(2) << "Updating cdc state table with: checkpoint: " << commit_op_id.ToString()
              << ", last active time: " << last_active_time
              << ", for tablet: " << producer_tablet.tablet_id
              << ", and stream: " << producer_tablet.stream_id;
    }

    // Only perform the update if we have a row in cdc_state to prevent a race condition where
    // a stream is deleted and then this logic inserts entries in cdc_state from that deleted
    // stream.
    auto* condition = req->mutable_if_expr()->mutable_condition();
    condition->set_op(QL_OP_EXISTS);
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    RETURN_NOT_OK(RefreshCacheOnFail(session->TEST_ApplyAndFlush(op)));
  }

  return Status::OK();
}

const std::string GetCDCMetricsKey(const std::string& stream_id) {
  return "CDCMetrics::" + stream_id;
}

std::shared_ptr<CDCTabletMetrics> CDCServiceImpl::GetCDCTabletMetrics(
    const ProducerTabletInfo& producer,
    std::shared_ptr<tablet::TabletPeer> tablet_peer,
    CreateCDCMetricsEntity create) {
  // 'nullptr' not recommended: using for tests.
  if (tablet_peer == nullptr) {
    auto status = tablet_manager_->GetTabletPeer(producer.tablet_id, &tablet_peer);
    if (!status.ok() || tablet_peer == nullptr) return nullptr;
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
      attrs["stream_id"] = producer.stream_id;
    }
    auto entity = METRIC_ENTITY_cdc.Instantiate(metric_registry_, producer.MetricsString(), attrs);
    metrics_raw = std::make_shared<CDCTabletMetrics>(entity);
    // Adding the new metric to the tablet so it maintains the same lifetime scope.
    tablet->AddAdditionalMetadata(key, metrics_raw);
  }

  return std::static_pointer_cast<CDCTabletMetrics>(metrics_raw);
}

void CDCServiceImpl::RemoveCDCTabletMetrics(
    const ProducerTabletInfo& producer,
    std::shared_ptr<tablet::TabletPeer> tablet_peer) {
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
  tablet->RemoveAdditionalMetadata(key);
}

Result<std::shared_ptr<StreamMetadata>> CDCServiceImpl::GetStream(
    const std::string& stream_id, bool ignore_cache) {
  if (!ignore_cache) {
    auto stream = GetStreamMetadataFromCache(stream_id);
    if (stream != nullptr) {
      return stream;
    }
  }

  // Look up stream in sys catalog.
  std::vector<ObjectId> object_ids;
  NamespaceId ns_id;
  std::unordered_map<std::string, std::string> options;
  RETURN_NOT_OK(client()->GetCDCStream(stream_id, &ns_id, &object_ids, &options));

  auto stream_metadata = std::make_shared<StreamMetadata>();

  AddDefaultOptionsIfMissing(&options);

  for (const auto& option : options) {
    if (option.first == kRecordType) {
      SCHECK(CDCRecordType_Parse(option.second, &stream_metadata->record_type),
             IllegalState, "CDC record type parsing error");
    } else if (option.first == kRecordFormat) {
      SCHECK(CDCRecordFormat_Parse(option.second, &stream_metadata->record_format),
             IllegalState, "CDC record format parsing error");
    } else if (option.first == kSourceType) {
      SCHECK(CDCRequestSource_Parse(option.second, &stream_metadata->source_type), IllegalState,
             "CDC record format parsing error");
    } else if (option.first == kCheckpointType) {
      SCHECK(CDCCheckpointType_Parse(option.second, &stream_metadata->checkpoint_type),
             IllegalState, "CDC record format parsing error");
    } else if (option.first == cdc::kIdType && option.second == cdc::kNamespaceId) {
      stream_metadata->ns_id = ns_id;
      stream_metadata->table_ids.insert(
          stream_metadata->table_ids.end(), object_ids.begin(), object_ids.end());
    } else if (option.first == cdc::kIdType && option.second == cdc::kTableId) {
      stream_metadata->table_ids.insert(
          stream_metadata->table_ids.end(), object_ids.begin(), object_ids.end());
    } else {
      LOG(WARNING) << "Unsupported CDC option: " << option.first;
    }
  }

  AddStreamMetadataToCache(stream_id, stream_metadata);
  return stream_metadata;
}

void CDCServiceImpl::RemoveStreamFromCache(const CDCStreamId& stream_id) {
  std::lock_guard<decltype(mutex_)> l(mutex_);
  stream_metadata_.erase(stream_id);
}

void CDCServiceImpl::AddStreamMetadataToCache(
    const std::string& stream_id, const std::shared_ptr<StreamMetadata>& metadata) {
  std::lock_guard<decltype(mutex_)> l(mutex_);
  stream_metadata_.emplace(stream_id, metadata);
}

std::shared_ptr<StreamMetadata> CDCServiceImpl::GetStreamMetadataFromCache(
    const std::string& stream_id) {
  SharedLock<decltype(mutex_)> l(mutex_);
  auto it = stream_metadata_.find(stream_id);
  if (it != stream_metadata_.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

Status CDCServiceImpl::CheckTabletValidForStream(const ProducerTabletInfo& info) {
  auto result = VERIFY_RESULT(impl_->PreCheckTabletValidForStream(info));
  if (result) {
    return Status::OK();
  }
  // If we don't recognize the tablet_id, populate our full tablet list for this stream.
  // This can happen if we call "GetChanges" on a split tablet. We will initalise the entries for
  // the split tablets in both: tablet_checkpoints_ and cdc_state_metadata_.
  auto tablets = VERIFY_RESULT(GetTablets(info.stream_id));

  auto status = impl_->CheckTabletValidForStream(info, tablets);

  if (status.IsInvalidArgument()) {
    // We check and see if tablet split has occured on the tablet.
    for (const auto& tablet : tablets) {
      if (tablet.has_split_parent_tablet_id() &&
          tablet.split_parent_tablet_id() == info.tablet_id) {
        return STATUS_FORMAT(TabletSplit, Format("Tablet Split detected on $0", info.tablet_id));
      }
    }
  }

  return status;
}

void CDCServiceImpl::TabletLeaderIsBootstrapRequired(
        const IsBootstrapRequiredRequestPB* req,
        IsBootstrapRequiredResponsePB* resp,
        rpc::RpcContext* context,
        const std::shared_ptr<tablet::TabletPeer>& peer) {
  auto result = GetLeaderTServer(peer->tablet_id());
  RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                            CDCErrorPB::TABLET_NOT_FOUND, *context);

  auto ts_leader = *result;
  // Check that tablet leader identified by master is not current tablet peer.
  // This can happen during tablet rebalance if master and tserver have different views of
  // leader. We need to avoid self-looping in this case.
  if (peer) {
    RPC_CHECK_NE_AND_RETURN_ERROR(ts_leader->permanent_uuid(), peer->permanent_uuid(),
                                  STATUS(IllegalState,
                                        Format("Tablet leader changed: leader=$0, peer=$1",
                                                ts_leader->permanent_uuid(),
                                                peer->permanent_uuid())),
                                  resp->mutable_error(), CDCErrorPB::NOT_LEADER, *context);
  }

  auto cdc_proxy = GetCDCServiceProxy(ts_leader);
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));

  IsBootstrapRequiredRequestPB new_req;
  new_req.set_stream_id(req->stream_id());
  new_req.add_tablet_ids(peer->tablet_id());
  auto status = cdc_proxy->IsBootstrapRequired(new_req, resp, &rpc);
  RPC_STATUS_RETURN_ERROR(status, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, *context);

  context->RespondSuccess();
}

void CDCServiceImpl::IsBootstrapRequired(const IsBootstrapRequiredRequestPB* req,
                                         IsBootstrapRequiredResponsePB* resp,
                                         rpc::RpcContext context) {
  RPC_CHECK_AND_RETURN_ERROR(req->tablet_ids_size() > 0,
                             STATUS(InvalidArgument,
                                    "Tablet ID is required to check for replication"),
                             resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  for (auto& tablet_id : req->tablet_ids()) {
    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    auto s = tablet_manager_->GetTabletPeer(tablet_id, &tablet_peer);
    if (s.IsNotFound() || !IsTabletPeerLeader(tablet_peer)) {
      LOG_WITH_FUNC(INFO) << "Not the leader for " << tablet_id << ".  Running proxy query.";
      TabletLeaderIsBootstrapRequired(req, resp, &context, tablet_peer);
      continue;
    }

    auto session = client()->NewSession();
    CoarseTimePoint deadline = GetDeadline(context, client());

    session->SetDeadline(deadline);
    OpId op_id = OpId();

    std::shared_ptr<CDCTabletMetrics> tablet_metric = NULL;

    if (req->has_stream_id() && !req->stream_id().empty()) {
      // Check that requested tablet_id is part of the CDC stream.
      ProducerTabletInfo producer_tablet = {"" /* UUID */, req->stream_id(), tablet_id};
      s = CheckTabletValidForStream(producer_tablet);
      RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

      auto result = GetLastCheckpoint(producer_tablet, session);
      if (result.ok()) {
        op_id = *result;
      }

      tablet_metric = GetCDCTabletMetrics(producer_tablet, tablet_peer);
    }

    auto log = tablet_peer->log();
    if (op_id.index == log->GetLatestEntryOpId().index) {
      // Consumer has caught up to producer
      continue;
    }

    int64_t next_index = op_id.index + 1;
    consensus::ReplicateMsgs replicates;
    int64_t starting_op_segment_seq_num;
    yb::SchemaPB schema;
    uint32_t schema_version;

    auto log_result = log->GetLogReader()->ReadReplicatesInRange(next_index,
                                                                next_index,
                                                                0,
                                                                &replicates,
                                                                &starting_op_segment_seq_num,
                                                                &schema,
                                                                &schema_version);

    // TODO: We should limit this to the specific Status error associated with missing logs.
    bool missing_logs = !log_result.ok();
    if (missing_logs) {
      LOG(INFO) << "Couldn't read " << next_index << ". Bootstrap required for tablet "
                << tablet_peer->tablet_id() << ": " << log_result.ToString();
      resp->set_bootstrap_required(missing_logs);
    }
    if (tablet_metric) {
      tablet_metric->is_bootstrap_required->set_value(missing_logs ? 1 : 0);
    }
  }
  context.RespondSuccess();
}

Status CDCServiceImpl::UpdateChildrenTabletsOnSplitOpForCDCSDK(const ProducerTabletInfo& info) {
  auto tablets = VERIFY_RESULT(GetTablets(info.stream_id));
  const OpId& children_op_id = OpId();

  std::array<const master::TabletLocationsPB*, 2> children_tablets;
  uint found_children = 0;
  for (auto const& tablet : tablets) {
    if (tablet.has_split_parent_tablet_id() && tablet.split_parent_tablet_id() == info.tablet_id) {
      children_tablets[found_children] = &tablet;
      found_children += 1;

      if (found_children == 2) {
        break;
      }
    }
  }
  LOG_IF(DFATAL, found_children != 2)
      << "Could not find the two split children for the tablet: " << info.tablet_id;

  // Add the entries for the children tablets in 'cdc_state_metadata_' and 'tablet_checkpoints_'.
  RETURN_NOT_OK_SET_CODE(
      impl_->AddEntriesForChildrenTabletsOnSplitOp(info, children_tablets, children_op_id),
      CDCError(CDCErrorPB::INTERNAL_ERROR));
  VLOG(1) << "Added entries for children tablets: " << children_tablets[0]->tablet_id() << " and "
          << children_tablets[1]->tablet_id() << ", of parent tablet: " << info.tablet_id
          << ", to 'cdc_state_metadata_' and 'tablet_checkpoints_'";

  return Status::OK();
}

Status CDCServiceImpl::UpdateChildrenTabletsOnSplitOp(
    const ProducerTabletInfo& producer_tablet,
    std::shared_ptr<yb::consensus::ReplicateMsg> split_op_msg,
    const client::YBSessionPtr& session) {
  const auto split_req = split_op_msg->split_request();
  const auto parent_tablet = split_req.tablet_id();
  const vector<string> children_tablets = {split_req.new_tablet1_id(), split_req.new_tablet2_id()};

  auto cdc_state_table = VERIFY_RESULT(GetCdcStateTable());
  // First check if the children tablet entries exist yet in cdc_state.
  for (const auto& child_tablet : children_tablets) {
    const auto op = cdc_state_table->NewReadOp();
    auto* const req = op->mutable_request();
    QLAddStringHashValue(req, child_tablet);

    auto cond = req->mutable_where_expr()->mutable_condition();
    cond->set_op(QLOperator::QL_OP_AND);
    QLAddStringCondition(cond, Schema::first_column_id() + master::kCdcStreamIdIdx,
        QL_OP_EQUAL, producer_tablet.stream_id);
    req->mutable_column_refs()->add_ids(Schema::first_column_id() + master::kCdcTabletIdIdx);
    req->mutable_column_refs()->add_ids(Schema::first_column_id() + master::kCdcStreamIdIdx);
    cdc_state_table->AddColumns({master::kCdcCheckpoint}, req);

    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    RETURN_NOT_OK(RefreshCacheOnFail(session->TEST_ReadSync(op)));

    auto row_block = ql::RowsResult(op.get()).GetRowBlock();
    SCHECK(row_block->row_count() == 1, NotFound,
           Format("Error finding entry in cdc_state table for tablet: $0, stream $1.",
                  child_tablet, producer_tablet.stream_id));
  }

  // Force an update of parent tablet checkpoint/timestamp to ensure that there it gets updated at
  // least once (otherwise, we may have a situation where consecutive splits occur within the
  // cdc_state table update window, and we wouldn't update the tablet's row with non-null values).
  impl_->ForceCdcStateUpdate(producer_tablet);

  // If we found both entries then lets update their checkpoints to this split_op's op id, to
  // ensure that we continue replicating from where we left off.
  for (const auto& child_tablet : children_tablets) {
    const auto op = cdc_state_table->NewUpdateOp();
    auto* const req = op->mutable_request();
    QLAddStringHashValue(req, child_tablet);
    QLAddStringRangeValue(req, producer_tablet.stream_id);
    // No need to update the timestamp here as we haven't started replicating the child yet.
    cdc_state_table->AddStringColumnValue(
        req, master::kCdcCheckpoint, consensus::OpIdToString(split_op_msg->id()));
    // Only perform updates from tservers for cdc_state, so check if row exists or not.
    auto* condition = req->mutable_if_expr()->mutable_condition();
    condition->set_op(QL_OP_EXISTS);
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    RETURN_NOT_OK(RefreshCacheOnFail(session->TEST_ApplyAndFlush(op)));
  }

  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
