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

#include "yb/master/master_snapshot_coordinator.h"

#include <unordered_map>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/asio/io_context.hpp>

#include "yb/common/common_types_util.h"
#include "yb/common/constants.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/dockv/doc_key.h"
#include "yb/docdb/rocksdb_writer.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/master/async_snapshot_tasks.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_error.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/restoration_state.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/snapshot_coordinator_context.h"
#include "yb/master/snapshot_schedule_state.h"
#include "yb/master/snapshot_state.h"
#include "yb/master/sys_catalog_writer.h"

#include "yb/rpc/poller.h"
#include "yb/rpc/scheduler.h"

#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_snapshots.h"
#include "yb/tablet/write_query.h"

#include "yb/util/async_util.h"
#include "yb/util/flags.h"
#include "yb/util/pb_util.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"

using std::vector;
using std::string;

using namespace std::literals;
using namespace std::placeholders;

DECLARE_int32(sys_catalog_write_timeout_ms);
DECLARE_bool(enable_fast_pitr);

DEFINE_UNKNOWN_uint64(snapshot_coordinator_poll_interval_ms, 5000,
              "Poll interval for snapshot coordinator in milliseconds.");

DEFINE_test_flag(bool, skip_sending_restore_finished, false,
                 "Whether we should skip sending RESTORE_FINISHED to tablets.");

DEFINE_RUNTIME_bool(schedule_snapshot_rpcs_out_of_band, true,
    "Should tablet snapshot RPCs be scheduled out of band from the periodic"
    " background scheduling.");

DEFINE_RUNTIME_bool(schedule_restoration_rpcs_out_of_band, true,
    "Should tablet restoration RPCs be scheduled out of band from the periodic"
    " background scheduling.");

DEFINE_RUNTIME_bool(skip_crash_on_duplicate_snapshot, false,
    "Should we not crash when we get a create snapshot request with the same "
    "id as one of the previous snapshots.");

DEFINE_test_flag(int32, delay_sys_catalog_restore_on_followers_secs, 0,
                 "Sleep for these many seconds on followers during sys catalog restore");

DEFINE_test_flag(bool, fatal_on_snapshot_verify, true,
                 "Whether to use DFATAL to log error messages when verifying a snapshot.");

namespace yb {
namespace master {

namespace {

YB_DEFINE_ENUM(Bound, (kFirst)(kLast));

Status SubmitWrite(
    docdb::KeyValueWriteBatchPB&& write_batch, int64_t leader_term,
    SnapshotCoordinatorContext* context,
    const std::shared_ptr<Synchronizer>& synchronizer = nullptr) {
  auto query = std::make_unique<tablet::WriteQuery>(
      leader_term, CoarseMonoClock::now() + FLAGS_sys_catalog_write_timeout_ms * 1ms,
      /* context */ nullptr, /* tablet= */ nullptr, /* rpc_context= */ nullptr);
  if (synchronizer) {
    query->set_callback(
        tablet::MakeWeakSynchronizerOperationCompletionCallback(synchronizer));
  }
  *query->operation().AllocateRequest()->mutable_write_batch() = std::move(write_batch);
  return context->Submit(query.release()->PrepareSubmit(), leader_term);
}

Status SynchronizedWrite(
    docdb::KeyValueWriteBatchPB&& write_batch, int64_t leader_term, CoarseTimePoint deadline,
    SnapshotCoordinatorContext* context) {
  auto synchronizer = std::make_shared<Synchronizer>();
  RETURN_NOT_OK(SubmitWrite(std::move(write_batch), leader_term, context, synchronizer));
  return synchronizer->WaitUntil(ToSteady(deadline));
}

struct RestorationData {
  TxnSnapshotId snapshot_id;
  TxnSnapshotRestorationId restoration_id;
  HybridTime restore_at;
  std::optional<int64_t> db_oid;
};

struct NoOp {
  template <class... Args>
  void operator()(Args&&... args) const {}
};

// Utility to create callback that is invoked when operation done.
// Finds appropriate entry in passed collection and invokes Done on it.
template <class Collection, class PostProcess = NoOp>
auto MakeDoneCallback(
    std::mutex* mutex, const Collection& collection, const typename Collection::key_type& key,
    const TabletId& tablet_id, const PostProcess& post_process = PostProcess()) {
  struct DoneFunctor {
    std::mutex& mutex;
    const Collection& collection;
    typename Collection::key_type key;
    TabletId tablet_id;
    PostProcess post_process;

    void operator()(Result<const tserver::TabletSnapshotOpResponsePB&> resp) const {
      std::unique_lock<std::mutex> lock(mutex);
      auto it = collection.find(key);
      if (it == collection.end()) {
        LOG(DFATAL) << "Received reply for unknown " << key;
        return;
      }

      (**it).Done(tablet_id, ResultToStatus(resp));
      post_process(it->get(), &lock);
    }
  };

  return DoneFunctor {
    .mutex = *mutex,
    .collection = collection,
    .key = key,
    .tablet_id = tablet_id,
    .post_process = post_process,
  };
}

bool SnapshotSuitableForRestoreAt(const SysSnapshotEntryPB& entry, HybridTime restore_at) {
  return (entry.state() == master::SysSnapshotEntryPB::COMPLETE ||
          entry.state() == master::SysSnapshotEntryPB::CREATING) &&
         HybridTime::FromPB(entry.snapshot_hybrid_time()) >= restore_at;
}

Result<TxnSnapshotId> FindSnapshotSuitableForRestoreAt(
    const SnapshotScheduleInfoPB& schedule, HybridTime restore_at) {
  if (schedule.snapshots().empty()) {
    return STATUS(NotFound, "Schedule does not have any snapshots.");
  }
  std::vector<std::reference_wrapper<const SnapshotInfoPB>> snapshots(
      schedule.snapshots().begin(), schedule.snapshots().end());
  // Don't rely on the implementation sorting the snapshots by creation time.
  std::sort(
      snapshots.begin(), snapshots.end(), [](const SnapshotInfoPB& lhs, const SnapshotInfoPB& rhs) {
        return HybridTime::FromPB(lhs.entry().snapshot_hybrid_time()) <
               HybridTime::FromPB(rhs.entry().snapshot_hybrid_time());
      });
  if (restore_at < HybridTime::FromPB(snapshots[0].get().entry().snapshot_hybrid_time())) {
    const auto& earliest_snapshot = snapshots[0];
    return STATUS_FORMAT(
        IllegalState,
        "Trying to restore to $0 which is earlier than the configured retention. "
        "Not allowed. Earliest snapshot that can be used is $1 and was taken at $2.",
        restore_at, TryFullyDecodeTxnSnapshotId(earliest_snapshot.get().id()),
        HybridTime::FromPB(earliest_snapshot.get().entry().snapshot_hybrid_time()));
  }
  // Return the id of the oldest valid snapshot created before the restore_at time.
  auto it = std::find_if(
      snapshots.begin(), snapshots.end(), [&restore_at](const SnapshotInfoPB& snapshot) {
        return SnapshotSuitableForRestoreAt(snapshot.entry(), restore_at);
      });
  if (it != snapshots.end()) {
    return FullyDecodeTxnSnapshotId(it->get().id());
  }
  return STATUS_FORMAT(
      NotFound,
      "The schedule does not have any valid snapshots created after the restore at time.");
}

} // namespace

class MasterSnapshotCoordinator::Impl {
 public:
  explicit Impl(SnapshotCoordinatorContext* context, CatalogManager* cm)
      : context_(*context), cm_(cm), poller_(std::bind(&Impl::Poll, this)) {}

  Result<TxnSnapshotId> Create(
      const SysRowEntries& entries, bool imported, int64_t leader_term, CoarseTimePoint deadline) {
    auto synchronizer = std::make_shared<Synchronizer>();
    auto snapshot_id = TxnSnapshotId::GenerateRandom();
    RETURN_NOT_OK(SubmitCreate(
        entries, imported, SnapshotScheduleId::Nil(), HybridTime::kInvalid, snapshot_id,
        leader_term,
        tablet::MakeWeakSynchronizerOperationCompletionCallback(synchronizer)));
    RETURN_NOT_OK(synchronizer->WaitUntil(ToSteady(deadline)));

    return snapshot_id;
  }

  Result<TxnSnapshotId> CreateForSchedule(
      const SnapshotScheduleId& schedule_id, int64_t leader_term, CoarseTimePoint deadline) {
    boost::optional<SnapshotScheduleOperation> operation;
    {
      std::lock_guard lock(mutex_);
      auto it = schedules_.find(schedule_id);
      if (it == schedules_.end()) {
        return STATUS_FORMAT(NotFound, "Unknown snapshot schedule: $0", schedule_id);
      }
      auto* last_snapshot = BoundingSnapshot((**it).id(), Bound::kLast);
      auto last_snapshot_time = last_snapshot ? last_snapshot->snapshot_hybrid_time()
                                              : HybridTime::kInvalid;
      auto creating_snapshot_data = (**it).creating_snapshot_data();
      if (creating_snapshot_data.snapshot_id) {
        auto snapshot_it = snapshots_.find(creating_snapshot_data.snapshot_id);
        if (snapshot_it != snapshots_.end()) {
          VLOG(2) << __func__ << " for " << schedule_id << " while creating snapshot: "
                  << (**snapshot_it).ToString();
        } else {
          auto passed = CoarseMonoClock::now() - creating_snapshot_data.start_time;
          auto message = Format(
              "$0 for $1 while creating unknown snapshot: $2 (passed $3)",
              __func__, schedule_id, creating_snapshot_data.snapshot_id, passed);
          if (passed > 30s) {
            LOG(DFATAL) << message;
          } else {
            VLOG(2) << message;
          }
        }
      }
      operation = VERIFY_RESULT((**it).ForceCreateSnapshot(last_snapshot_time));
    }

    auto synchronizer = std::make_shared<Synchronizer>();
    RETURN_NOT_OK(ExecuteScheduleOperation(*operation, leader_term, synchronizer));
    RETURN_NOT_OK(synchronizer->WaitUntil(ToSteady(deadline)));

    return operation->snapshot_id;
  }

  Status CreateReplicated(int64_t leader_term, const tablet::SnapshotOperation& operation) {
    // TODO(txn_backup) retain logs with this operation while doing snapshot
    auto id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(operation.request()->snapshot_id()));

    VLOG(1) << __func__ << "(" << id << ", " << operation.ToString() << ")";

    auto snapshot = std::make_unique<SnapshotState>(
        &context_, id, operation.request()->ToGoogleProtobuf(),
        GetRpcLimit(FLAGS_max_concurrent_snapshot_rpcs,
                    FLAGS_max_concurrent_snapshot_rpcs_per_tserver, leader_term));

    TabletSnapshotOperations operations;
    docdb::KeyValueWriteBatchPB write_batch;
    RETURN_NOT_OK(snapshot->StoreToWriteBatch(&write_batch));
    boost::optional<tablet::CreateSnapshotData> sys_catalog_snapshot_data;
    bool snapshot_empty = false;
    {
      std::lock_guard lock(mutex_);
      auto emplace_result = snapshots_.emplace(std::move(snapshot));
      if (!emplace_result.second) {
        LOG(INFO) << "Received a duplicate create snapshot request for id: " << id;
        if (FLAGS_skip_crash_on_duplicate_snapshot) {
          LOG(INFO) << "Skipping duplicate create snapshot request gracefully.";
          return Status::OK();
        } else {
          return STATUS_FORMAT(IllegalState, "Duplicate snapshot id: $0", id);
        }
      }

      if (leader_term >= 0) {
        (**emplace_result.first).PrepareOperations(&operations);
      }
      auto temp = (**emplace_result.first).SysCatalogSnapshotData(operation);
      if (temp.ok()) {
        sys_catalog_snapshot_data = *temp;
      } else if (!temp.status().IsUninitialized()) {
        return temp.status();
      }
      snapshot_empty = (**emplace_result.first).Empty();
    }

    auto tablet = VERIFY_RESULT(operation.tablet_safe());
    RETURN_NOT_OK(tablet->ApplyOperation(
        operation, /* batch_idx= */ -1, *rpc::CopySharedMessage(write_batch)));
    if (sys_catalog_snapshot_data) {
      RETURN_NOT_OK(tablet->snapshots().Create(*sys_catalog_snapshot_data));
    }

    ExecuteOperations(operations, leader_term);

    if (leader_term >= 0 && snapshot_empty) {
      // There could be snapshot for 0 tables, so they should be marked as complete right after
      // creation.
      UpdateSnapshotIfPresent(id, leader_term);
    }

    return Status::OK();
  }

  void UpdateSnapshotIfPresent(const TxnSnapshotId& id, int64_t leader_term)
      NO_THREAD_SAFETY_ANALYSIS EXCLUDES(mutex_) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = snapshots_.find(id);
    if (it != snapshots_.end()) {
      UpdateSnapshot(it->get(), leader_term, &lock);
    }
  }

  template <class Pb, class Map>
  Status LoadEntryOfType(
      tablet::Tablet* tablet, const SysRowEntryType& type, Map* m) REQUIRES(mutex_) {
    return EnumerateSysCatalog(tablet, context_.schema(), type,
        [this, &m](const Slice& id, const Slice& data) NO_THREAD_SAFETY_ANALYSIS -> Status {
          return LoadEntry<Pb>(id, data, m);
    });
  }

  Status Load(tablet::Tablet* tablet) {
    std::lock_guard lock(mutex_);
    RETURN_NOT_OK(LoadEntryOfType<SysSnapshotEntryPB>(
        tablet, SysRowEntryType::SNAPSHOT, &snapshots_));
    RETURN_NOT_OK(LoadEntryOfType<SnapshotScheduleOptionsPB>(
        tablet, SysRowEntryType::SNAPSHOT_SCHEDULE, &schedules_));
    return LoadEntryOfType<SysRestorationEntryPB>(
        tablet, SysRowEntryType::SNAPSHOT_RESTORATION, &restorations_);
  }

  Status ApplyWritePair(Slice key, const Slice& value) {
    dockv::SubDocKey sub_doc_key;
    RETURN_NOT_OK(sub_doc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kFalse));

    if (sub_doc_key.doc_key().has_cotable_id()) {
      return Status::OK();
    }

    if (sub_doc_key.doc_key().range_group().size() != 2) {
      LOG(DFATAL) << "Unexpected size of range group in sys catalog entry (2 expected): "
                  << AsString(sub_doc_key.doc_key().range_group()) << "(" << sub_doc_key.ToString()
                  << ")";
      return Status::OK();
    }

    auto first_key = sub_doc_key.doc_key().range_group().front();
    if (first_key.type() != dockv::KeyEntryType::kInt32) {
      LOG(DFATAL) << "Unexpected value type for the first range component of sys catalog entry "
                  << "(kInt32 expected): "
                  << AsString(sub_doc_key.doc_key().range_group());;
    }

    switch (first_key.GetInt32()) {
      case SysRowEntryType::SNAPSHOT:
        return DoApplyWrite<SysSnapshotEntryPB>(
            sub_doc_key.doc_key().range_group()[1].GetString(), value, &snapshots_);

      case SysRowEntryType::SNAPSHOT_SCHEDULE:
        return DoApplyWrite<SnapshotScheduleOptionsPB>(
            sub_doc_key.doc_key().range_group()[1].GetString(), value, &schedules_);

      case SysRowEntryType::SNAPSHOT_RESTORATION:
        return DoApplyWrite<SysRestorationEntryPB>(
            sub_doc_key.doc_key().range_group()[1].GetString(), value, &restorations_);

      default:
        return Status::OK();
    }
  }

  template <class Pb, class Map>
  Status DoApplyWrite(const std::string& id_str, const Slice& value, Map* map) {
    dockv::Value decoded_value;
    RETURN_NOT_OK(decoded_value.Decode(value));

    auto value_type = decoded_value.primitive_value().value_type();

    if (value_type == dockv::ValueEntryType::kTombstone) {
      std::lock_guard lock(mutex_);
      auto id = Uuid::TryFullyDecode(id_str);
      if (id.IsNil()) {
        LOG(WARNING) << "Unable to decode id: " << id_str;
        return Status::OK();
      }
      bool erased = map->erase(typename Map::key_type(id)) != 0;
      LOG_IF(DFATAL, !erased) << "Unknown entry tombstoned: " << id.ToString();
      return Status::OK();
    }

    if (value_type != dockv::ValueEntryType::kString) {
      return STATUS_FORMAT(
          Corruption,
          "Bad value type: $0, expected kString while replaying write for sys catalog",
          decoded_value.primitive_value().value_type());
    }

    std::lock_guard lock(mutex_);
    return LoadEntry<Pb>(id_str, decoded_value.primitive_value().GetString(), map);
  }

  Status ListSnapshots(
      const TxnSnapshotId& snapshot_id, bool list_deleted,
      ListSnapshotsDetailOptionsPB options, ListSnapshotsResponsePB* resp) {
    std::lock_guard lock(mutex_);
    if (snapshot_id.IsNil()) {
      for (const auto& p : snapshots_.get<ScheduleTag>()) {
        if (!list_deleted) {
          auto aggreaged_state = p->AggregatedState();
          if (aggreaged_state.ok() && *aggreaged_state == SysSnapshotEntryPB::DELETED) {
            continue;
          }
        }
        RETURN_NOT_OK(p->ToPB(resp->add_snapshots(), options));
      }
      return Status::OK();
    }

    SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(snapshot_id));
    return snapshot.ToPB(resp->add_snapshots(), options);
  }

  Status Delete(
      const TxnSnapshotId& snapshot_id, int64_t leader_term, CoarseTimePoint deadline) {
    VLOG_WITH_FUNC(4) << snapshot_id << ", " << leader_term;

    {
      std::lock_guard lock(mutex_);
      SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(snapshot_id));
      RETURN_NOT_OK(snapshot.TryStartDelete());
    }

    auto synchronizer = std::make_shared<Synchronizer>();
    RETURN_NOT_OK(SubmitDelete(snapshot_id, leader_term, synchronizer));
    return synchronizer->WaitUntil(ToSteady(deadline));
  }

  // Abort tasks on each tablet and mark them as FAILED to ensure that we dont create
  // AsyncSnapshotOps for them later on.
  Status AbortRestore(const TxnSnapshotRestorationId& restoration_id,
      int64_t leader_term, CoarseTimePoint deadline) {
    VLOG_WITH_FUNC(4) << restoration_id.ToString() << ", " << leader_term;
    {
      std::lock_guard lock(mutex_);
      auto restoration_ptr = &VERIFY_RESULT(FindRestoration(restoration_id)).get();
      RETURN_NOT_OK_PREPEND(restoration_ptr->Abort(), "Failed to abort tasks");

      // Update restoration entry to sys catalog.
      LOG(INFO) << Format(
          "Marking restoration $0 as FAILED in sys catalog", restoration_id.ToString());
      docdb::KeyValueWriteBatchPB write_batch;
      RETURN_NOT_OK_PREPEND(
          restoration_ptr->StoreToWriteBatch(&write_batch),
          "Failed to prepare write batch for snapshot");
      RETURN_NOT_OK_PREPEND(
          SubmitWrite(std::move(write_batch), leader_term, &context_),
          "Failed to submit snapshot abort operation");
    }

    return Status::OK();
  }

  Status DeleteReplicated(int64_t leader_term, const tablet::SnapshotOperation& operation) {
    auto snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(operation.request()->snapshot_id()));
    VLOG_WITH_FUNC(4) << leader_term << ", " << snapshot_id;

    docdb::KeyValueWriteBatchPB write_batch;
    TabletSnapshotOperations operations;
    bool delete_sys_catalog_snapshot;
    {
      std::lock_guard lock(mutex_);
      SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(snapshot_id));
      if (snapshot.schedule_id()) {
        delete_sys_catalog_snapshot = true;
      }
      snapshot.SetInitialTabletsState(SysSnapshotEntryPB::DELETING);
      RETURN_NOT_OK(snapshot.StoreToWriteBatch(&write_batch));
      if (leader_term >= 0) {
        snapshot.PrepareOperations(&operations);
      }
    }

    auto tablet = VERIFY_RESULT(operation.tablet_safe());
    if (delete_sys_catalog_snapshot) {
      RETURN_NOT_OK(tablet->snapshots().Delete(operation));
    }

    RETURN_NOT_OK(tablet->ApplyOperation(
        operation, /* batch_idx= */ -1, *rpc::CopySharedMessage(write_batch)));

    ExecuteOperations(operations, leader_term);
    return Status::OK();
  }

  Status RestoreSysCatalogReplicated(
      int64_t leader_term, const tablet::SnapshotOperation& operation, Status* complete_status) {
    auto restoration = std::make_shared<SnapshotScheduleRestoration>(SnapshotScheduleRestoration {
      .snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(operation.request()->snapshot_id())),
      .restore_at = HybridTime::FromPB(operation.request()->snapshot_hybrid_time()),
      .restoration_id = VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(
          operation.request()->restoration_id())),
      .op_id = operation.op_id(),
      .write_time = operation.hybrid_time(),
      .term = leader_term,
      .db_oid = std::nullopt,
      .schedules = {},
      .non_system_obsolete_tablets = {},
      .non_system_obsolete_tables = {},
      .non_system_objects_to_restore = {},
      .existing_system_tables = {},
      .restoring_system_tables = {},
      .parent_to_child_tables = {},
      .non_system_tablets_to_restore = {},
    });
    {
      std::lock_guard lock(mutex_);
      SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(restoration->snapshot_id));
      SnapshotScheduleState& schedule_state = VERIFY_RESULT(
          FindSnapshotSchedule(snapshot.schedule_id()));
      LOG(INFO) << "Restore sys catalog from snapshot: " << snapshot.ToString() << ", schedule: "
                << schedule_state.ToString() << " at " << restoration->restore_at << ", op id: "
                << restoration->op_id;
      size_t this_idx = std::numeric_limits<size_t>::max();
      for (const auto& snapshot_schedule : schedules_) {
        if (snapshot_schedule->id() == snapshot.schedule_id()) {
          this_idx = restoration->schedules.size();
        }
        restoration->schedules.emplace_back(
            snapshot_schedule->id(), snapshot_schedule->options().filter());
      }
      if (this_idx == std::numeric_limits<size_t>::max()) {
        return STATUS_FORMAT(IllegalState, "Cannot find schedule for restoration: $0",
                             snapshot.schedule_id());
      }
      std::swap(restoration->schedules[0], restoration->schedules[this_idx]);
    }
    // Inject artificial delay on followers for tests.
    if (leader_term < 0 && FLAGS_TEST_delay_sys_catalog_restore_on_followers_secs > 0) {
      SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_delay_sys_catalog_restore_on_followers_secs));
    }
    // Disable concurrent RPCs to the master leader for the duration of sys catalog restore.
    if (leader_term >= 0) {
      context_.PrepareRestore();
    }
    auto tablet = VERIFY_RESULT(operation.tablet_safe());
    LOG_SLOW_EXECUTION(INFO, 1000, "Restore sys catalog took") {
      RETURN_NOT_OK_PREPEND(
          context_.RestoreSysCatalog(restoration.get(), tablet.get(), complete_status),
          "Restore sys catalog failed");
    }
    return Status::OK();
  }

  Status ListRestorations(
      const TxnSnapshotRestorationId& restoration_id, const TxnSnapshotId& snapshot_id,
      ListSnapshotRestorationsResponsePB* resp) {
    std::lock_guard lock(mutex_);
    if (!restoration_id) {
      for (const auto& p : restorations_) {
        if (!snapshot_id || p->snapshot_id() == snapshot_id) {
          RETURN_NOT_OK(p->ToPB(resp->add_restorations()));
        }
      }
      return Status::OK();
    }

    RestorationState& restoration = VERIFY_RESULT(FindRestoration(restoration_id));
    return restoration.ToPB(resp->add_restorations());
  }

  Result<TxnSnapshotRestorationId> Restore(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at, int64_t leader_term) {
    auto restoration_id = TxnSnapshotRestorationId::GenerateRandom();
    RETURN_NOT_OK(DoRestore(
        snapshot_id, restore_at, restoration_id, RestorePhase::kInitial, leader_term));
    return restoration_id;
  }

  Result<SnapshotScheduleId> CreateSchedule(
      const CreateSnapshotScheduleRequestPB& req, int64_t leader_term, CoarseTimePoint deadline) {
    // Get the validated table from the request.
    const auto& table = VERIFY_RESULT(ParseCreateSnapshotScheduleRequest(req));

    // Fail the request if at least one keyspace in the requested snapshot schedules already exists.
    const std::string& namespace_name = table.get().namespace_().name();
    const YQLDatabase namespace_type = table.get().namespace_().database_type();
    {
      std::lock_guard lock(mutex_);
      const auto& existing_schedule = FindSnapshotSchedule(namespace_name, namespace_type);
      if (existing_schedule.ok()) {
        return STATUS(AlreadyPresent,
                      Format("Snapshot schedule $0 already exists for the given keyspace $1.$2",
                             existing_schedule->id(), DatabaseTypeName(namespace_type),
                             namespace_name),
                      MasterError(MasterErrorPB::OBJECT_ALREADY_PRESENT));
      }
    }

    auto schedule = VERIFY_RESULT(SnapshotScheduleState::Create(&context_, req.options()));
    docdb::KeyValueWriteBatchPB write_batch;
    RETURN_NOT_OK(schedule.StoreToWriteBatch(&write_batch));
    RETURN_NOT_OK(SynchronizedWrite(std::move(write_batch), leader_term, deadline, &context_));
    return schedule.id();
  }

  Result<const TableIdentifierPB&> ParseCreateSnapshotScheduleRequest(
      const CreateSnapshotScheduleRequestPB& req) {
    if (req.options().filter().tables().tables_size() == 0) {
      return STATUS(InvalidArgument, "Request must contain a keyspace",
                    MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    if (req.options().filter().tables().tables_size() > 1) {
      return STATUS(InvalidArgument, "Request cannot contain more than one keyspace",
                    MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    const auto& table = req.options().filter().tables().tables()[0];

    if (!table.has_namespace_()) {
      return STATUS(InvalidArgument, "Request does not contain a keyspace. Snapshot schedules must "
                    "be created at the db level", MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    if (table.namespace_().database_type() != YQLDatabase::YQL_DATABASE_CQL &&
        table.namespace_().database_type() != YQLDatabase::YQL_DATABASE_PGSQL) {
      return STATUS(InvalidArgument, "The keyspace must be of type YSQL or YCQL",
                    MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    if (table.namespace_().name().empty()) {
      return STATUS(InvalidArgument, "The keyspace name must be non-empty",
                    MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    return table;
  }

  Status ListSnapshotSchedules(
      const SnapshotScheduleId& snapshot_schedule_id, ListSnapshotSchedulesResponsePB* resp) {
    std::lock_guard lock(mutex_);
    if (snapshot_schedule_id.IsNil()) {
      for (const auto& p : schedules_) {
        RETURN_NOT_OK(FillSchedule(*p, resp->add_schedules()));
      }
      return Status::OK();
    }

    SnapshotScheduleState& schedule = VERIFY_RESULT(FindSnapshotSchedule(snapshot_schedule_id));
    return FillSchedule(schedule, resp->add_schedules());
  }

  Status DeleteSnapshotSchedule(
      const SnapshotScheduleId& snapshot_schedule_id, int64_t leader_term,
      CoarseTimePoint deadline) {
    docdb::KeyValueWriteBatchPB write_batch;
    {
      std::lock_guard lock(mutex_);
      SnapshotScheduleState& schedule = VERIFY_RESULT(FindSnapshotSchedule(snapshot_schedule_id));
      SnapshotScheduleOptionsPB updated_options = schedule.options();
      updated_options.set_delete_time(context_.Clock()->Now().ToUint64());
      RETURN_NOT_OK(schedule.StoreToWriteBatch(updated_options, &write_batch));
    }
    return SynchronizedWrite(std::move(write_batch), leader_term, deadline, &context_);
  }

  Result<SnapshotScheduleInfoPB> EditSnapshotSchedule(
      const SnapshotScheduleId& id, const EditSnapshotScheduleRequestPB& req, int64_t leader_term,
      CoarseTimePoint deadline) {
    docdb::KeyValueWriteBatchPB write_batch;
    {
      std::lock_guard lock(mutex_);
      SnapshotScheduleState& schedule = VERIFY_RESULT(FindSnapshotSchedule(id));
      auto updated_options = VERIFY_RESULT(schedule.GetUpdatedOptions(req));
      RETURN_NOT_OK(schedule.StoreToWriteBatch(updated_options, &write_batch));
    }
    auto status = SynchronizedWrite(std::move(write_batch), leader_term, deadline, &context_);
    std::lock_guard lock(mutex_);
    SnapshotScheduleState& schedule = VERIFY_RESULT(FindSnapshotSchedule(id));
    SnapshotScheduleInfoPB result;
    RETURN_NOT_OK(FillSchedule(schedule, &result));
    return result;
  }

  Result<TxnSnapshotId> SuitableSnapshotId(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at, int64_t leader_term,
      CoarseTimePoint deadline) {
    while (CoarseMonoClock::now() < deadline) {
      ListSnapshotSchedulesResponsePB resp;
      RETURN_NOT_OK_PREPEND(ListSnapshotSchedules(schedule_id, &resp),
                            "Failed to list snapshot schedules");
      if (resp.schedules().size() < 1) {
        return STATUS_FORMAT(InvalidArgument, "Unknown schedule: $0", schedule_id);
      }
      auto snapshot_result = FindSnapshotSuitableForRestoreAt(resp.schedules()[0], restore_at);
      if (snapshot_result.ok()) {
        return *snapshot_result;
      } else if (!snapshot_result.status().IsNotFound()) {
        return snapshot_result;
      }
      auto snapshot_id = CreateForSchedule(schedule_id, leader_term, deadline);
      if (!snapshot_id.ok() &&
          MasterError(snapshot_id.status()) == MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION) {
        continue;
      }
      return snapshot_id;
    }

    return STATUS_FORMAT(
        TimedOut, "Timed out getting a suitable snapshot id from schedule $0", schedule_id);
  }

  Status RestoreSnapshotSchedule(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at,
      RestoreSnapshotScheduleResponsePB* resp, int64_t leader_term, CoarseTimePoint deadline) {
    auto snapshot_id =
        VERIFY_RESULT(SuitableSnapshotId(schedule_id, restore_at, leader_term, deadline));

    bool suitable_snapshot_is_complete = false;
    while (CoarseMonoClock::now() < deadline) {
      ListSnapshotsResponsePB resp;

      RETURN_NOT_OK_PREPEND(ListSnapshots(snapshot_id, false, {}, &resp),
                            "Failed to list snapshots");
      if (resp.snapshots().size() != 1) {
        return STATUS_FORMAT(
            IllegalState, "Wrong number of snapshots received $0", resp.snapshots().size());
      }

      if (resp.snapshots()[0].entry().state() == master::SysSnapshotEntryPB::COMPLETE) {
        if (SnapshotSuitableForRestoreAt(resp.snapshots()[0].entry(), restore_at)) {
          suitable_snapshot_is_complete = true;
          break;
        }
        return STATUS_FORMAT(
            IllegalState, "Snapshot is not suitable for restore at $0", restore_at);
      }
      std::this_thread::sleep_for(100ms);
    }

    if (!suitable_snapshot_is_complete) {
      return STATUS_FORMAT(
          TimedOut, "Timed out completing a snapshot $0", snapshot_id);
    }

    TxnSnapshotRestorationId restoration_id = VERIFY_RESULT(Restore(
        snapshot_id, restore_at, leader_term));

    resp->set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    resp->set_restoration_id(restoration_id.data(), restoration_id.size());

    return Status::OK();
  }

  Status FillHeartbeatResponse(TSHeartbeatResponsePB* resp) {
    std::lock_guard lock(mutex_);
    auto* out = resp->mutable_snapshots_info();
    for (const auto& schedule : schedules_) {
      // Don't send deleted schedules.
      if (schedule->deleted()) {
        continue;
      }
      const auto& id = schedule->id();
      auto* out_schedule = out->add_schedules();
      out_schedule->set_id(id.data(), id.size());
      auto time = LastSnapshotTime(id);
      if (time) {
        out_schedule->set_last_snapshot_hybrid_time(time.ToUint64());
      }
    }
    out->set_last_restorations_update_ht(last_restorations_update_ht_.ToUint64());
    for (const auto& restoration : restorations_) {
      auto* out_restoration = out->add_restorations();
      const auto& id = restoration->restoration_id();
      out_restoration->set_id(id.data(), id.size());
      auto complete_time = restoration->complete_time();
      if (complete_time) {
        out_restoration->set_complete_time_ht(complete_time.ToUint64());
      }
    }
    return Status::OK();
  }

  docdb::HistoryCutoff AllowedHistoryCutoffProvider(
      tablet::RaftGroupMetadata* metadata) {
    HybridTime min_last_snapshot_ht = HybridTime::kMax;
    HybridTime min_retention = HybridTime::kMax;
    std::lock_guard lock(mutex_);
    for (const auto& schedule : schedules_) {
      if (schedule->deleted()) {
        continue;
      }
      int64_t retention = schedule->options().retention_duration_sec();
      HybridTime retention_ht = context_.Clock()->Now().AddSeconds(-retention);
      min_retention.MakeAtMost(retention_ht);
      auto complete_time = LastSnapshotTime(schedule->id());
      // No snapshot yet for the schedule so retain everything.
      if (!complete_time) {
        min_last_snapshot_ht = HybridTime::kMin;
        continue;
      }
      min_last_snapshot_ht.MakeAtMost(complete_time);
    }
    if (GetAtomicFlag(&FLAGS_enable_fast_pitr)) {
      return { min_retention, min_last_snapshot_ht };
    }
    return { min_last_snapshot_ht, min_last_snapshot_ht };
  }

  Status VerifyRestoration(RestorationState* restoration) REQUIRES(mutex_) {
    auto schedule_result = FindSnapshotSchedule(restoration->schedule_id());
    RETURN_NOT_OK_PREPEND(schedule_result, "Snapshot schedule not found.");

    return context_.VerifyRestoredObjects(
        restoration->MasterMetadata(), schedule_result->options().filter().tables().tables());
  }

  boost::optional<yb::master::SnapshotState&> ValidateRestoreAndGetSnapshot(
      RestorationState* restoration) REQUIRES(mutex_) {
    // Ignore if already completed.
    if (restoration->complete_time()) {
      return boost::none;
    }
    // If the restore is still undergoing the sys catalog phase.
    if (!restoration->IsSysCatalogRestorationDone()) {
      return boost::none;
    }
    // If the snapshot to restore is not ok.
    auto snapshot = FindSnapshot(restoration->snapshot_id());
    if (!snapshot.ok()) {
      LOG(DFATAL) << "Snapshot not found for pending restore with id "
                  << restoration->restoration_id();
      return boost::none;
    }

    return *snapshot;
  }

  void SendPendingRestoreRpcs(
      const vector<RestorationData>& postponed_restores, int64_t leader_term) {
    for (const auto& restoration : postponed_restores) {
      LOG(INFO) << "PITR: Issuing pending tserver RPCs for restoration "
                << restoration.restoration_id;
      auto status = DoRestore(restoration.snapshot_id, restoration.restore_at,
                              restoration.restoration_id,
                              RestorePhase::kPostSysCatalogLoad, leader_term,
                              restoration.db_oid);
      LOG_IF(DFATAL, !status.ok())
          << "Failed to restore tablets for restoration "
          << restoration.restoration_id << ": " << status;
    }
  }

  std::optional<int64_t> ComputeDbOid(RestorationState* restoration) REQUIRES(mutex_) {
    std::optional<int64_t> db_oid;
    bool contains_sequences_data = false;
    for (const auto& id_and_type : restoration->MasterMetadata()) {
      if (id_and_type.second == SysRowEntryType::NAMESPACE &&
          id_and_type.first == kPgSequencesDataNamespaceId) {
        contains_sequences_data = true;
      }
    }
    if (contains_sequences_data) {
      for (const auto& id_and_type : restoration->MasterMetadata()) {
        if (id_and_type.second == SysRowEntryType::NAMESPACE &&
            id_and_type.first != kPgSequencesDataNamespaceId) {
          auto db_oid_res = GetPgsqlDatabaseOid(id_and_type.first);
          LOG_IF(DFATAL, !db_oid_res.ok())
              << "Unable to obtain db_oid for namespace id "
              << id_and_type.first << ": " << db_oid_res.status();
          db_oid = static_cast<int64_t>(*db_oid_res);
          LOG(INFO) << "DB OID of restoring database " << *db_oid;
          // TODO(Sanket): In future can enhance to pass a list of db_oids.
          break;
        }
      }
      LOG_IF(DFATAL, !db_oid)
          << "Unable to fetch db oid for the restoring database";
    }
    return db_oid;
  }

  void SysCatalogLoaded(int64_t leader_term) {
    if (leader_term == OpId::kUnknownTerm) {
      // Do nothing on follower.
      return;
    }

    // Issue pending restoration rpcs.
    vector<RestorationData> postponed_restores;
    {
      std::lock_guard lock(mutex_);
      // TODO(pitr) cancel restorations.
      for (const auto& restoration : restorations_) {
        auto snapshot = ValidateRestoreAndGetSnapshot(restoration.get());
        if (!snapshot) {
          continue;
        }
        // If it is PITR restore, then verify restoration and add to the queue for rpcs.
        if (!restoration->schedule_id().IsNil()) {
          auto status = VerifyRestoration(restoration.get());
          if (status.ok()) {
            LOG(INFO) << "PITR: Master metadata verified successfully for restoration "
                      << restoration->restoration_id();
          } else {
            // We've had some instances in the past when verification failed but there was no issue
            // with restore. Especially with tablet splitting. In Retail mode just log an error and
            // proceed.
            auto error_msg = Format(
                "PITR: Master metadata verified failed for restoration $0, status: $1",
                restoration->restoration_id(), status);
            if (FLAGS_TEST_fatal_on_snapshot_verify) {
              LOG(DFATAL) << error_msg;
            } else {
              LOG(ERROR) << error_msg;
            }
          }

          auto db_oid = ComputeDbOid(restoration.get());
          postponed_restores.push_back(RestorationData {
            .snapshot_id = restoration->snapshot_id(),
            .restoration_id = restoration->restoration_id(),
            .restore_at = restoration->restore_at(),
            .db_oid = db_oid,
          });
        }
        // Set the throttling limits.
        restoration->Throttler().RefreshLimit(
            GetRpcLimit(FLAGS_max_concurrent_restoration_rpcs,
                        FLAGS_max_concurrent_restoration_rpcs_per_tserver, leader_term));
        // Set the current term so that the snapshot coordinator can pick it up.
        restoration->SetLeaderTerm(leader_term);
      }
    }
    SendPendingRestoreRpcs(postponed_restores, leader_term);
  }

  Result<SnapshotSchedulesToObjectIdsMap> MakeSnapshotSchedulesToObjectIdsMap(
      SysRowEntryType type) {
    std::vector<std::pair<SnapshotScheduleId, SnapshotScheduleFilterPB>> schedules;
    {
      std::lock_guard lock(mutex_);
      for (const auto& schedule : schedules_) {
        if (!schedule->deleted()) {
          schedules.emplace_back(schedule->id(), schedule->options().filter());
        }
      }
    }
    SnapshotSchedulesToObjectIdsMap result;
    for (const auto& id_and_filter : schedules) {
      auto entries = VERIFY_RESULT(CollectEntries(id_and_filter.second));
      auto& ids = result[id_and_filter.first];
      for (const auto& entry : entries.entries()) {
        if (entry.type() == type) {
          ids.push_back(entry.id());
        }
      }
      std::sort(ids.begin(), ids.end());
    }
    return result;
  }

  Result<bool> TableMatchesSchedule(
      const TableIdentifiersPB& table_identifiers, const SysTablesEntryPB& pb, const string& id) {
    for (const auto& table_identifier : table_identifiers.tables()) {
      if (VERIFY_RESULT(TableMatchesIdentifier(id, pb, table_identifier))) {
        return true;
      }
    }
    return false;
  }

  Result<bool> IsTableCoveredBySomeSnapshotSchedule(const TableInfo& table_info) {
    auto lock = table_info.LockForRead();
    {
      std::lock_guard l(mutex_);
      for (const auto& schedule : schedules_) {
        if (VERIFY_RESULT(TableMatchesSchedule(
                schedule->options().filter().tables(), lock->pb, table_info.id()))) {
          return true;
        }
      }
    }
    return false;
  }

  Result<bool> IsTableUndergoingPitrRestore(const TableInfo& table_info) {
    {
      std::lock_guard l(mutex_);
      for (const auto& restoration : restorations_) {
        // If restore does not have a snapshot schedule then it
        // is not a PITR restore.
        if (!restoration->schedule_id()) {
          continue;
        }
        // If PITR restore is already complete.
        if (restoration->complete_time()) {
          continue;
        }
        // Ongoing PITR restore.
        SnapshotScheduleState& schedule = VERIFY_RESULT(
            FindSnapshotSchedule(restoration->schedule_id()));
        {
          auto lock = table_info.LockForRead();
          if (VERIFY_RESULT(TableMatchesSchedule(
                  schedule.options().filter().tables(), lock->pb, table_info.id()))) {
            return true;
          }
        }
      }
    }
    return false;
  }

  bool IsPitrActive() {
    std::lock_guard lock(mutex_);
    for (const auto& schedule : schedules_) {
      if (!schedule->deleted()) {
        return true;
      }
    }
    return false;
  }

  Result<docdb::KeyValuePairPB> UpdateRestorationAndGetWritePair(
      SnapshotScheduleRestoration* restoration) {
    std::lock_guard lock(mutex_);
    RestorationState* restoration_ptr =
        VERIFY_RESULT(GetRestorationPtrOrNull(restoration->restoration_id));
    // Won't be found on followers.
    if (!restoration_ptr) {
      LOG(INFO) << "Constructing new restoration state for id " << restoration->restoration_id;
      SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(restoration->snapshot_id));
      if (!VERIFY_RESULT(snapshot.Complete())) {
        return STATUS(IllegalState, "The snapshot state is not complete",
                      restoration->snapshot_id.ToString(),
                      MasterError(MasterErrorPB::SNAPSHOT_IS_NOT_READY));
      }
      auto restoration_state = std::make_unique<RestorationState>(
          &context_, restoration->restoration_id, &snapshot,
          restoration->restore_at, IsSysCatalogRestored::kTrue,
          GetRpcLimit(FLAGS_max_concurrent_restoration_rpcs,
                      FLAGS_max_concurrent_restoration_rpcs_per_tserver, restoration->term));
      restoration_ptr = restorations_.emplace(std::move(restoration_state)).first->get();
    }
    // Set the phase to post sys catalog restoration.
    restoration_ptr->SetSysCatalogRestored(IsSysCatalogRestored::kTrue);
    // Reset the tablet list since it could have changed from the time of snapshot
    // to restoration time. Also, set the metadata for verifying restoration
    // post sys catalog load.
    std::vector<TabletId> restore_tablets;
    for (const auto& id_and_type : restoration->non_system_objects_to_restore) {
      restoration_ptr->MutableMasterMetadata()->emplace(id_and_type.first, id_and_type.second);
      if (id_and_type.second == SysRowEntryType::TABLET) {
        restore_tablets.push_back(id_and_type.first);
      }
    }
    LOG(INFO) << "PITR: " << restoration->restoration_id
              << ", tablets to restore: " << AsString(restore_tablets);
    restoration_ptr->InitTabletIds(restore_tablets);
    // Prepare write batch.
    docdb::KeyValuePairPB restore_kv;
    RETURN_NOT_OK(restoration_ptr->StoreToKeyValuePair(&restore_kv));

    return restore_kv;
  }

  void Start() {
    {
      std::lock_guard lock(mutex_);
      last_restorations_update_ht_ = context_.Clock()->Now();
    }
    poller_.Start(&context_.Scheduler(), FLAGS_snapshot_coordinator_poll_interval_ms * 1ms);
  }

  void Shutdown() {
    poller_.Shutdown();
  }

 private:
  template <class Pb, class Map>
  Status LoadEntry(const Slice& id_slice, const Slice& data, Map* map) REQUIRES(mutex_) {
    VLOG(2) << __func__ << "(" << id_slice.ToDebugString() << ", " << data.ToDebugString() << ")";

    auto id = Uuid::TryFullyDecode(id_slice);
    if (id.IsNil()) {
      return Status::OK();
    }
    auto metadata = VERIFY_RESULT(pb_util::ParseFromSlice<Pb>(data));
    return LoadEntry(typename Map::key_type(id), metadata, map);
  }

  template <class Pb, class Map>
  Status LoadEntry(
      const typename Map::key_type& id, const Pb& data, Map* map)
      REQUIRES(mutex_) {
    VLOG(1) << __func__ << "(" << id << ", " << data.ShortDebugString() << ")";

    auto new_entry = std::make_unique<typename Map::value_type::element_type>(&context_, id, data);

    auto it = map->find(id);
    if (it == map->end()) {
      map->emplace(std::move(new_entry));
    } else if ((**it).ShouldUpdate(*new_entry)) {
      map->replace(it, std::move(new_entry));
    } else {
      VLOG_WITH_FUNC(1) << "Ignore because of version check, existing: " << (**it).ToString()
                        << ", loaded: " << new_entry->ToString();
    }

    return Status::OK();
  }

  Result<SnapshotState&> FindSnapshot(const TxnSnapshotId& snapshot_id) REQUIRES(mutex_) {
    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
      return STATUS(NotFound, "Could not find snapshot", snapshot_id.ToString(),
                    MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
    }
    return **it;
  }

  Result<RestorationState&> FindRestoration(
      const TxnSnapshotRestorationId& restoration_id) REQUIRES(mutex_) {
    auto it = restorations_.find(restoration_id);
    if (it == restorations_.end()) {
      return STATUS(NotFound, "Could not find restoration", restoration_id.ToString(),
                    MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    return **it;
  }

  Result<RestorationState*> GetRestorationPtrOrNull(
      const TxnSnapshotRestorationId& restoration_id) REQUIRES(mutex_) {
    auto restoration_result = FindRestoration(restoration_id);
    if (!restoration_result.ok() && restoration_result.status().IsNotFound()) {
      return nullptr;
    }
    RETURN_NOT_OK(restoration_result);
    return &*restoration_result;
  }

  Result<SnapshotScheduleState&> FindSnapshotSchedule(
      const SnapshotScheduleId& id) REQUIRES(mutex_) {
    auto it = schedules_.find(id);
    if (it == schedules_.end()) {
      return STATUS(NotFound, "Could not find snapshot schedule", id.ToString(),
                    MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
    }
    return **it;
  }

  Result<SnapshotScheduleState&> FindSnapshotSchedule(
      const std::string& namespace_name,
      const YQLDatabase namespace_type,
      const bool ignore_deleted = true) REQUIRES(mutex_) {
    for (const auto& schedule : schedules_) {
      if (ignore_deleted && schedule->deleted()) {
        continue;
      }

      DCHECK_EQ(schedule->options().filter().tables().tables_size(), 1);
      if (schedule->options().filter().tables().tables_size() != 1) {
        LOG(WARNING) << Format("Detected a schedule with an unexpected number of identifiers: $1",
                               schedule->options().filter().tables().tables_size());
      }

      for (const auto& table : schedule->options().filter().tables().tables()) {
        if (table.namespace_().name() == namespace_name &&
            table.namespace_().database_type() == namespace_type) {
          return *schedule;
        }
      }
    }

    return STATUS(NotFound, "Could not find snapshot schedule", namespace_name,
                    MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  void ExecuteOperations(const TabletSnapshotOperations& operations, int64_t leader_term) {
    if (operations.empty()) {
      return;
    }
    VLOG(4) << __func__ << "(" << AsString(operations) << ")";

    size_t num_operations = operations.size();
    LOG(INFO) << "Number of snapshot operations to be executed " << num_operations;
    std::vector<TabletId> tablet_ids;
    tablet_ids.reserve(num_operations);
    for (const auto& operation : operations) {
      tablet_ids.push_back(operation.tablet_id);
    }
    auto tablet_infos = context_.GetTabletInfos(tablet_ids);
    for (size_t i = 0; i != num_operations; ++i) {
      ExecuteOperation(operations[i], tablet_infos[i], leader_term);
    }
  }

  void ExecuteOperation(
      const TabletSnapshotOperation& operation, const TabletInfoPtr& tablet_info,
      int64_t leader_term) {
    auto callback = MakeDoneCallback(
        &mutex_, snapshots_, operation.snapshot_id, operation.tablet_id,
        std::bind(&Impl::UpdateSnapshot, this, _1, leader_term, _2));
    if (!tablet_info) {
      callback(STATUS_EC_FORMAT(NotFound, MasterError(MasterErrorPB::TABLET_NOT_RUNNING),
                                "Tablet info not found for $0", operation.tablet_id));
      return;
    }
    auto snapshot_id_str = operation.snapshot_id.AsSlice().ToBuffer();

    auto epoch = LeaderEpoch(leader_term, context_.pitr_count());
    if (operation.state == SysSnapshotEntryPB::DELETING) {
      auto task = context_.CreateAsyncTabletSnapshotOp(
          tablet_info, snapshot_id_str, tserver::TabletSnapshotOpRequestPB::DELETE_ON_TABLET,
          epoch, callback);
      context_.ScheduleTabletSnapshotOp(task);
    } else if (operation.state == SysSnapshotEntryPB::CREATING) {
      auto task = context_.CreateAsyncTabletSnapshotOp(
          tablet_info, snapshot_id_str, tserver::TabletSnapshotOpRequestPB::CREATE_ON_TABLET, epoch,
          callback);
      task->SetSnapshotScheduleId(operation.schedule_id);
      task->SetSnapshotHybridTime(operation.snapshot_hybrid_time);
      context_.ScheduleTabletSnapshotOp(task);
    } else {
      LOG(DFATAL) << "Unsupported snapshot operation: " << operation.ToString();
    }
  }

  void ExecuteRestoreOperations(
      const TabletRestoreOperations& operations, int64_t leader_term) {
    if (operations.empty()) {
      return;
    }

    size_t num_operations = operations.size();
    LOG(INFO) << "Number of tablet restore operations to be executed " << num_operations;
    std::vector<TabletId> tablet_ids;
    tablet_ids.reserve(num_operations);
    for (const auto& operation : operations) {
      tablet_ids.push_back(operation.tablet_id);
    }
    auto tablet_infos = context_.GetTabletInfos(tablet_ids);
    for (size_t i = 0; i != num_operations; ++i) {
      ExecuteRestoreOperation(
          operations[i], tablet_infos[i], leader_term);
    }
  }

  template <typename TableContainer>
  void SetTaskMetadataForColocatedTable(
      const TableContainer& table_ids, AsyncTabletSnapshotOp* task) {
    for (const auto& table_id : table_ids) {
      auto table_info_result = context_.GetTableById(table_id);
      // TODO(Sanket): Should make this check FATAL once GHI#14609 is fixed.
      if (!table_info_result.ok()) {
        LOG(WARNING) << "Table " << table_id << " does not exist";
        continue;
      }
      // Ignore if deleted or hidden.
      if (!(*table_info_result)->IsOperationalForClient()) {
        LOG(WARNING) << "Table " << table_id << " has been deleted";
        continue;
      }
      task->SetColocatedTableMetadata(table_id, (*table_info_result)->LockForRead()->pb);
    }
  }

  void ExecuteRestoreOperation(
      const TabletRestoreOperation& operation, const TabletInfoPtr& tablet_info,
      int64_t leader_term) {
    auto callback = MakeDoneCallback(
        &mutex_, restorations_, operation.restoration_id, operation.tablet_id,
        std::bind(&Impl::FinishRestoration, this, _1, leader_term));
    if (!tablet_info) {
      callback(STATUS_EC_FORMAT(
          NotFound, MasterError(MasterErrorPB::TABLET_NOT_RUNNING), "Tablet info not found for $0",
          operation.tablet_id));
      return;
    }
    auto snapshot_id_str = operation.snapshot_id.AsSlice().ToBuffer();
    // If this tablet did not participate in snapshot, i.e. was deleted.
    // We just change hybrid time limit and clear hide state.
    auto epoch = LeaderEpoch(leader_term, context_.pitr_count());
    auto task = context_.CreateAsyncTabletSnapshotOp(
        tablet_info, operation.is_tablet_part_of_snapshot ? snapshot_id_str : std::string(),
        tserver::TabletSnapshotOpRequestPB::RESTORE_ON_TABLET, epoch, callback);
    task->SetSnapshotHybridTime(operation.restore_at);
    task->SetRestorationId(operation.restoration_id);
    if (!operation.schedule_id.IsNil()) {
      task->SetSnapshotScheduleId(operation.schedule_id);
    }
    if (operation.sys_catalog_restore_needed) {
      task->SetMetadata(tablet_info->table()->LockForRead()->pb);
      // Populate metadata for colocated tables.
      if (tablet_info->colocated()) {
        auto lock = tablet_info->LockForRead();
        if (lock->pb.hosted_tables_mapped_by_parent_id()) {
          SetTaskMetadataForColocatedTable(tablet_info->GetTableIds(), task.get());
        } else {
          SetTaskMetadataForColocatedTable(lock->pb.table_ids(), task.get());
        }
      }
    }
    // For sequences_data_table, we should set partial restore and db_oid.
    if (tablet_info->table()->id() == kPgSequencesDataTableId) {
      LOG_IF(DFATAL, !operation.db_oid) << "DB OID not found for restoring database";
      task->SetDbOid(*operation.db_oid);
    }
    context_.ScheduleTabletSnapshotOp(task);
  }

  struct PollSchedulesData {
    std::vector<TxnSnapshotId> delete_snapshots;
    SnapshotScheduleOperations schedule_operations;
    ScheduleMinRestoreTime schedule_min_restore_time;
  };

  void Poll() {
    auto leader_term = context_.LeaderTerm();
    if (leader_term < 0) {
      return;
    }
    SCOPED_LEADER_SHARED_LOCK(l, cm_);
    if (!l.IsInitializedAndIsLeader()) {
      return;
    }
    VLOG(4) << __func__ << "()";
    std::vector<TxnSnapshotId> cleanup_snapshots;
    TabletSnapshotOperations operations;
    TabletRestoreOperations restore_operations;
    PollSchedulesData schedules_data;
    {
      std::lock_guard lock(mutex_);
      for (const auto& p : snapshots_) {
        if (p->NeedCleanup()) {
          LOG(INFO) << "Cleanup of snapshot " << p->id() << " started.";
          if (!p->CleanupTracker().Start().ok()) {
            LOG(DFATAL) << "Cleanup of snapshot " << p->id() << " was already started.";
          }
          cleanup_snapshots.push_back(p->id());
        } else {
          // Refresh the throttle limit.
          p->Throttler().RefreshLimit(
              GetRpcLimit(FLAGS_max_concurrent_snapshot_rpcs,
                          FLAGS_max_concurrent_snapshot_rpcs_per_tserver, leader_term));
          p->PrepareOperations(&operations);
        }
      }
      PollSchedulesPrepare(&schedules_data);
      for (const auto& r : restorations_) {
        auto snapshot = ValidateRestoreAndGetSnapshot(r.get());
        if (!snapshot || r->GetLeaderTerm() != leader_term) {
          continue;
        }
        r->Throttler().RefreshLimit(
            GetRpcLimit(FLAGS_max_concurrent_restoration_rpcs,
                        FLAGS_max_concurrent_restoration_rpcs_per_tserver, leader_term));
        auto tablets = (*snapshot).tablet_ids();
        std::unordered_set<TabletId> tablets_snapshot(tablets.begin(), tablets.end());
        std::optional<int64_t> db_oid = std::nullopt;
        if (!snapshot->schedule_id().IsNil()) {
          db_oid = ComputeDbOid(r.get());
        }
        r->PrepareOperations(&restore_operations, tablets_snapshot, db_oid);
      }
    }
    for (const auto& id : cleanup_snapshots) {
      CleanupObject(leader_term, id, snapshots_, EncodedSnapshotKey(id, &context_));
    }
    ExecuteOperations(operations, leader_term);
    PollSchedulesComplete(schedules_data, l.epoch());
    ExecuteRestoreOperations(restore_operations, leader_term);
  }

  void TryDeleteSnapshot(SnapshotState* snapshot, PollSchedulesData* data) {
    auto delete_status = snapshot->TryStartDelete();
    if (!delete_status.ok()) {
      VLOG(1) << "Unable to delete snapshot " << snapshot->id() << "/" << snapshot->schedule_id()
              << ": " << delete_status << ", state: " << snapshot->ToString();
      return;
    }

    VLOG(1) << "Cleanup snapshot: " << snapshot->id() << "/" << snapshot->schedule_id();
    data->delete_snapshots.push_back(snapshot->id());
  }

  void PollSchedulesPrepare(PollSchedulesData* data) REQUIRES(mutex_) {
    auto now = context_.Clock()->Now();
    for (const auto& p : schedules_) {
      HybridTime last_snapshot_time;
      if (p->deleted()) {
        auto range = snapshots_.get<ScheduleTag>().equal_range(p->id());
        for (const auto& snapshot : boost::make_iterator_range(range.first, range.second)) {
          TryDeleteSnapshot(snapshot.get(), data);
        }
      } else {
        auto& index = snapshots_.get<ScheduleTag>();
        auto range = index.equal_range(p->id());
        if (range.first != range.second) {
          --range.second;
          for (; range.first != range.second; ++range.first) {
            if ((**range.first).initial_state() != SysSnapshotEntryPB::DELETING) {
              break;
            }
          }
          auto& first_snapshot = **range.first;
          data->schedule_min_restore_time[p->id()] =
              first_snapshot.previous_snapshot_hybrid_time()
                  ? first_snapshot.previous_snapshot_hybrid_time()
                  : first_snapshot.snapshot_hybrid_time();
          auto gc_limit = now.AddSeconds(-p->options().retention_duration_sec());
          VLOG_WITH_FUNC(4) << "Gc limit: " << gc_limit;
          for (; range.first != range.second; ++range.first) {
            if ((**range.first).snapshot_hybrid_time() >= gc_limit) {
              break;
            }
            TryDeleteSnapshot(range.first->get(), data);
          }
          last_snapshot_time = (**range.second).snapshot_hybrid_time();
        }
      }
      p->PrepareOperations(last_snapshot_time, now, &data->schedule_operations);
    }
  }

  void PollSchedulesComplete(const PollSchedulesData& data, const LeaderEpoch& epoch)
      EXCLUDES(mutex_) {
    for (const auto& id : data.delete_snapshots) {
      Status submit_status = SubmitDelete(id, epoch.leader_term, /* synchronizer */ nullptr);
      // TODO(submit_error): is this sufficient to handle this error?
      LOG_IF(DFATAL, !submit_status.ok())
          << "Failed to submit delete operation for snapshot " << id << ". "
          << submit_status;
    }
    for (const auto& operation : data.schedule_operations) {
      switch (operation.type) {
        case SnapshotScheduleOperationType::kCreateSnapshot:
          WARN_NOT_OK(ExecuteScheduleOperation(operation, epoch.leader_term),
                      Format("Failed to execute operation on $0", operation.schedule_id));
          break;
        case SnapshotScheduleOperationType::kCleanup:
          CleanupObject(
              epoch.leader_term, operation.schedule_id, schedules_,
              SnapshotScheduleState::EncodedKey(operation.schedule_id, &context_));
          break;
        default:
          LOG(DFATAL) << "Unexpected operation type: " << operation.type;
          break;
      }
    }
    context_.CleanupHiddenObjects(data.schedule_min_restore_time, epoch);
  }

  SnapshotState* BoundingSnapshot(const SnapshotScheduleId& schedule_id, Bound bound)
      REQUIRES(mutex_) {
    auto& index = snapshots_.get<ScheduleTag>();
    decltype(index.begin()) it;
    if (bound == Bound::kFirst) {
      it = index.lower_bound(schedule_id);
      if (it == index.end()) {
        return nullptr;
      }
    } else {
      it = index.upper_bound(schedule_id);
      if (it == index.begin()) {
        return nullptr;
      }
      --it;
    }
    return (**it).schedule_id() == schedule_id ? it->get() : nullptr;
  }

  HybridTime LastSnapshotTime(const SnapshotScheduleId& schedule_id) REQUIRES(mutex_) {
    auto snapshot = BoundingSnapshot(schedule_id, Bound::kLast);
    return snapshot ? snapshot->snapshot_hybrid_time() : HybridTime::kInvalid;
  }

  template <typename Id, typename Map>
  void CleanupObjectAborted(Id id, const Map& map) {
    LOG(INFO) << "Aborting cleanup of object " << id;
    std::lock_guard l(mutex_);
    auto it = map.find(id);
    if (it == map.end()) {
      return;
    }
    (**it).CleanupTracker().Abort();
  }

  template <typename Map, typename Id>
  void CleanupObject(int64_t leader_term, Id id, const Map& map,
                     const Result<dockv::KeyBytes>& encoded_key) {
    if (!encoded_key.ok()) {
      LOG(DFATAL) << "Failed to encode id for deletion: " << encoded_key.status();
      return;
    }

    auto query = std::make_unique<tablet::WriteQuery>(
        leader_term, CoarseMonoClock::Now() + FLAGS_sys_catalog_write_timeout_ms * 1ms,
        nullptr /* context */, nullptr /* tablet */, nullptr /* rpc_context */);

    auto* write_batch = query->operation().AllocateRequest()->mutable_write_batch();
    auto pair = write_batch->add_write_pairs();
    pair->dup_key(encoded_key->AsSlice());
    char value = dockv::ValueEntryTypeAsChar::kTombstone;
    pair->dup_value(Slice(&value, 1));

    query->set_callback([this, id, &map](const Status& s) {
      if (s.ok()) {
        LOG(INFO) << "Finished cleanup of object " << id;
        return;
      }
      CleanupObjectAborted(id, map);
    });

    Status submit_status = context_.Submit(query.release()->PrepareSubmit(), leader_term);
    LOG_IF(DFATAL, !submit_status.ok())
        << "Failed to submit cleanup operation: " << submit_status;
  }

  Status ExecuteScheduleOperation(
      const SnapshotScheduleOperation& operation, int64_t leader_term,
      const std::weak_ptr<Synchronizer>& synchronizer = std::weak_ptr<Synchronizer>()) {
    auto entries = CollectEntries(operation.filter);
    VLOG(2) << __func__ << "(" << AsString(operation) << ", " << leader_term << "), entries: "
            << AsString(entries);
    if (!entries.ok()) {
      CreateSnapshotAborted(entries.status(), operation.schedule_id, operation.snapshot_id);
      return entries.status();
    }
    return SubmitCreate(
        *entries, /* imported= */ false, operation.schedule_id,
        operation.previous_snapshot_hybrid_time, operation.snapshot_id, leader_term,
        [this, schedule_id = operation.schedule_id, snapshot_id = operation.snapshot_id,
         synchronizer](
            const Status& status) {
          if (!status.ok()) {
            CreateSnapshotAborted(status, schedule_id, snapshot_id);
          }
          auto locked_synchronizer = synchronizer.lock();
          if (locked_synchronizer) {
            locked_synchronizer->StatusCB(status);
          }
        });
  }

  void CreateSnapshotAborted(
      const Status& status, const SnapshotScheduleId& schedule_id,
      const TxnSnapshotId& snapshot_id) {
    LOG(INFO) << __func__ << " for " << schedule_id << ", snapshot: " << snapshot_id
              << ", status: " << status;
    std::lock_guard lock(mutex_);
    auto it = schedules_.find(schedule_id);
    if (it == schedules_.end()) {
      return;
    }
    (**it).SnapshotFinished(snapshot_id, status);
  }

  Status SubmitCreate(
      const SysRowEntries& entries, bool imported, const SnapshotScheduleId& schedule_id,
      HybridTime previous_snapshot_hybrid_time, TxnSnapshotId snapshot_id, int64_t leader_term,
      tablet::OperationCompletionCallback completion_clbk) {
    auto operation = std::make_unique<tablet::SnapshotOperation>(/* tablet= */ nullptr);
    auto request = operation->AllocateRequest();

    VLOG(1) << __func__ << "(" << AsString(entries) << ", " << imported << ", " << schedule_id
            << ", " << snapshot_id << ")";
    // There could be more than one entry of the same tablet,
    // for instance in the case of colocated tables.
    std::unordered_set<TabletId> unique_tablet_ids;
    for (const auto& entry : entries.entries()) {
      if (entry.type() == SysRowEntryType::TABLET) {
        if (unique_tablet_ids.insert(entry.id()).second) {
          VLOG(1) << __func__ << "(Adding tablet " << entry.id()
                  << " for snapshot " << snapshot_id << ")";
          request->mutable_tablet_id()->push_back(request->arena().DupSlice(entry.id()));
        }
      }
    }

    request->set_snapshot_hybrid_time(context_.Clock()->MaxGlobalNow().ToUint64());
    request->set_operation(tserver::TabletSnapshotOpRequestPB::CREATE_ON_MASTER);
    request->dup_snapshot_id(snapshot_id.AsSlice());
    request->set_imported(imported);
    if (schedule_id) {
      request->dup_schedule_id(schedule_id.AsSlice());
    }
    if (previous_snapshot_hybrid_time) {
      request->set_previous_snapshot_hybrid_time(previous_snapshot_hybrid_time.ToUint64());
    }

    // TODO(lw_uc) implement PackFrom for LWAny.
    google::protobuf::Any temp_any;
    temp_any.PackFrom(entries);
    request->mutable_extra_data()->CopyFrom(temp_any);

    operation->set_completion_callback(std::move(completion_clbk));

    return context_.Submit(std::move(operation), leader_term);
  }

  Status SubmitDelete(const TxnSnapshotId& snapshot_id, int64_t leader_term,
                      const std::shared_ptr<Synchronizer>& synchronizer) {
    auto operation = std::make_unique<tablet::SnapshotOperation>(nullptr);
    auto request = operation->AllocateRequest();

    request->set_operation(tserver::TabletSnapshotOpRequestPB::DELETE_ON_MASTER);
    request->dup_snapshot_id(snapshot_id.AsSlice());

    operation->set_completion_callback(
        [this, wsynchronizer = std::weak_ptr<Synchronizer>(synchronizer), snapshot_id]
        (const Status& status) {
          auto synchronizer = wsynchronizer.lock();
          if (synchronizer) {
            synchronizer->StatusCB(status);
          }
          if (!status.ok()) {
            DeleteSnapshotAborted(status, snapshot_id);
          }
        });

    return context_.Submit(std::move(operation), leader_term);
  }

  Status SubmitRestore(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at,
      const TxnSnapshotRestorationId& restoration_id, int64_t leader_term) {
    auto synchronizer = std::make_shared<Synchronizer>();

    auto operation = std::make_unique<tablet::SnapshotOperation>(nullptr);
    auto request = operation->AllocateRequest();

    request->set_operation(tserver::TabletSnapshotOpRequestPB::RESTORE_SYS_CATALOG);
    request->dup_snapshot_id(snapshot_id.AsSlice());
    request->set_snapshot_hybrid_time(restore_at.ToUint64());
    if (restoration_id) {
      request->dup_restoration_id(restoration_id.AsSlice());
    }

    operation->set_completion_callback(
        tablet::MakeWeakSynchronizerOperationCompletionCallback(synchronizer));

    RETURN_NOT_OK(context_.Submit(std::move(operation), leader_term));

    return synchronizer->Wait();
  }

  void DeleteSnapshotAborted(
      const Status& status, const TxnSnapshotId& snapshot_id) {
    LOG(INFO) << __func__ << ", snapshot: " << snapshot_id << ", status: " << status;
    std::lock_guard lock(mutex_);
    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
      return;
    }
    (**it).DeleteAborted(status);
  }

  void UpdateSnapshot(
      SnapshotState* snapshot, int64_t leader_term, std::unique_lock<std::mutex>* lock)
      REQUIRES(mutex_) {
    bool batch_done = false;
    bool is_empty = snapshot->Empty();

    if (!is_empty) {
      batch_done = snapshot->Throttler().RemoveOutstandingTask();
    }
    if (!snapshot->AllTabletsDone()) {
      if (FLAGS_schedule_snapshot_rpcs_out_of_band && batch_done && !is_empty) {
        // Send another batch. This prevents having to wait for the regular cycle
        // of master snapshot coordinator which can be too slow.
        context_.Scheduler().io_service().post([this]() {
          LOG(INFO) << "Rescheduling Snapshot RPCs out of band.";
          Poll();
        });
      }
      return;
    }

    if (snapshot->schedule_id()) {
      UpdateSchedule(*snapshot);
    }

    docdb::KeyValueWriteBatchPB write_batch;
    auto status = snapshot->StoreToWriteBatch(&write_batch);
    if (!status.ok()) {
      LOG(DFATAL) << "Failed to prepare write batch for snapshot: " << status;
      return;
    }
    lock->unlock();

    Status submit_status = SubmitWrite(std::move(write_batch), leader_term, &context_);
    // TODO(submit_error): is this sufficient to handle this error?
    LOG_IF(DFATAL, !submit_status.ok())
        << "Failed to submit snapshot update operation: " << submit_status;
  };

  void FinishRestoration(RestorationState* restoration, int64_t leader_term) REQUIRES(mutex_) {
    bool tablet_list_empty = restoration->Empty();

    if (!tablet_list_empty) {
      bool batch_done = restoration->Throttler().RemoveOutstandingTask();
      if (!restoration->AllTabletsDone()) {
        if (FLAGS_schedule_restoration_rpcs_out_of_band && batch_done) {
          // Send another batch. This prevents having to wait for the regular cycle
          // of master snapshot coordinator which can be too slow.
          context_.Scheduler().io_service().post([this]() {
            LOG(INFO) << "Rescheduling restoration RPCs out of band.";
            Poll();
          });
        }
        return;
      }
    }

    last_restorations_update_ht_ = context_.Clock()->Now();
    restoration->set_complete_time(last_restorations_update_ht_);

    LOG(INFO) << "Setting restore complete time to " << last_restorations_update_ht_;

    if (restoration->schedule_id()) {
      auto schedule = FindSnapshotSchedule(restoration->schedule_id());
      if (schedule.ok()) {
        docdb::KeyValueWriteBatchPB write_batch;
        schedule->mutable_options().add_restoration_times(
            last_restorations_update_ht_.ToUint64());
        Status store_to_write_batch_status = schedule->StoreToWriteBatch(&write_batch);
        if (store_to_write_batch_status.ok()) {
          Status submit_status = SubmitWrite(std::move(write_batch), leader_term, &context_);
          if (!submit_status.ok()) {
            // TODO(submit_error): is this sufficient to handle this error?
            LOG(DFATAL) << "Failed to submit schedule update operation: " << submit_status;
            return;
          }
        } else {
          LOG(INFO) << "Unable to prepare write batch for schedule "
                    << schedule->id() << ": " << store_to_write_batch_status;
        }
      } else {
        LOG(INFO) << "Snapshot Schedule with id " << restoration->schedule_id()
                  << " not found" << ": " << schedule.status();
      }
    }

    if (FLAGS_TEST_skip_sending_restore_finished) {
      return;
    }

    std::vector<TabletId> tablet_ids = restoration->TabletIdsInState(SysSnapshotEntryPB::RESTORED);
    auto tablets = context_.GetTabletInfos(tablet_ids);
    auto epoch = LeaderEpoch(leader_term, context_.pitr_count());
    int tablet_ids_counter = 0;
    for (const auto& tablet : tablets) {
      if (tablet) {
        auto task = context_.CreateAsyncTabletSnapshotOp(
            tablet, std::string(), tserver::TabletSnapshotOpRequestPB::RESTORE_FINISHED, epoch,
            /* callback= */ nullptr);
        task->SetRestorationId(restoration->restoration_id());
        task->SetRestorationTime(restoration->complete_time());
        context_.ScheduleTabletSnapshotOp(task);
      } else {
        LOG(DFATAL) << Format("Tablet info not found for $0", tablet_ids[tablet_ids_counter]);
      }
      tablet_ids_counter++;
    }
    // Update restoration entry to sys catalog.
    LOG(INFO) << "Marking restoration " << restoration->restoration_id()
              << " as complete in sys catalog";
    docdb::KeyValueWriteBatchPB write_batch;
    auto status = restoration->StoreToWriteBatch(&write_batch);
    if (!status.ok()) {
      LOG(DFATAL) << "Failed to prepare write batch for snapshot: " << status;
      return;
    }
    status = SubmitWrite(std::move(write_batch), leader_term, &context_);
    if (!status.ok()) {
      // TODO(submit_error): is this sufficient to handle this error?
      LOG(DFATAL) << "Failed to submit snapshot operation: " << status;
      return;
    }

    // Resume index backfill for restored tables.
    // They are actually resumed by the catalog manager bg tasks thread.
    if (restoration->schedule_id()) {
      for (const auto& entry : restoration->MasterMetadata()) {
        if (entry.second == SysRowEntryType::TABLE) {
          context_.AddPendingBackFill(entry.first);
        }
      }
    }

    // Enable tablet splitting again.
    if (restoration->schedule_id()) {
      context_.ReenableTabletSplitting(kPitrFeatureName);
    }
  }

  void UpdateSchedule(const SnapshotState& snapshot) REQUIRES(mutex_) {
    auto it = schedules_.find(snapshot.schedule_id());
    if (it == schedules_.end()) {
      return;
    }

    auto state = snapshot.AggregatedState();
    Status status;
    if (!state.ok()) {
      status = state.status();
    } else {
      switch (*state) {
        case SysSnapshotEntryPB::COMPLETE:
          status = Status::OK();
          break;
        case SysSnapshotEntryPB::FAILED:
          status = snapshot.AnyFailure();
          break;
        case SysSnapshotEntryPB::DELETED:
          return;
        default:
          LOG(DFATAL) << "Unexpected snapshot state: " << *state << " for " << snapshot.id();
          return;
      }
    }
    (**it).SnapshotFinished(snapshot.id(), status);
  }

  Status FillSchedule(const SnapshotScheduleState& schedule, SnapshotScheduleInfoPB* out)
      REQUIRES(mutex_) {
    RETURN_NOT_OK(schedule.ToPB(out));
    const auto& index = snapshots_.get<ScheduleTag>();
    auto p = index.equal_range(boost::make_tuple(schedule.id()));
    for (auto i = p.first; i != p.second; ++i) {
      RETURN_NOT_OK((**i).ToPB(out->add_snapshots(), ListSnapshotsDetailOptionsPB()));
    }
    return Status::OK();
  }

  Result<SysRowEntries> CollectEntries(const SnapshotScheduleFilterPB& filter) {
    return context_.CollectEntriesForSnapshot(filter.tables().tables());
  }

  Status ForwardRestoreCheck(
      const SnapshotState& snapshot, HybridTime restore_at,
      const TxnSnapshotRestorationId& restoration_id) const REQUIRES(mutex_) {
    const auto& index = restorations_.get<ScheduleTag>();
    // Fetch all restorations under the given schedule id.
    auto restores = index.equal_range(snapshot.schedule_id());

    for (auto it = restores.first; it != restores.second; it++) {
      RestorationState* restore_state = it->get();
      if (restore_state->restore_at() <= restore_at &&
          restore_state->complete_time() >= restore_at) {
        std::string error_msg = Format(
            "Cannot perform a forward restore. Existing restoration $0 was restored to $1 "
            "and completed at $2, while the requested restoration for $3 is in between.",
            restore_state->restoration_id(), restore_state->restore_at(),
            restore_state->complete_time(), restore_at);

        LOG(WARNING) << error_msg;
        return STATUS(NotSupported, error_msg);
      }
    }

    return Status::OK();
  }

  Status SubmitRestoreWrite(RestorationState* restoration, int64_t leader_term) REQUIRES(mutex_) {
    LOG(INFO) << "Writing restoration " << restoration->restoration_id()
              << " entry to sys catalog";
    docdb::KeyValueWriteBatchPB write_batch;
    RETURN_NOT_OK(restoration->StoreToWriteBatch(&write_batch));
    RETURN_NOT_OK(SubmitWrite(std::move(write_batch), leader_term, &context_));
    return Status::OK();
  }

  Status DoRestore(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at,
      const TxnSnapshotRestorationId& restoration_id, RestorePhase phase, int64_t leader_term,
      std::optional<int64_t> db_oid = std::nullopt) {
    TabletRestoreOperations operations;
    bool restore_sys_catalog;
    std::unordered_set<TabletId> snapshot_tablets;
    bool tablet_list_empty = false;
    {
      std::lock_guard lock(mutex_);
      SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(snapshot_id));
      if (!VERIFY_RESULT(snapshot.Complete())) {
        return STATUS(IllegalState, "The snapshot state is not complete", snapshot_id.ToString(),
                      MasterError(MasterErrorPB::SNAPSHOT_IS_NOT_READY));
      }
      restore_sys_catalog = phase == RestorePhase::kInitial && !snapshot.schedule_id().IsNil();
      if (restore_sys_catalog) {
        RETURN_NOT_OK(ForwardRestoreCheck(snapshot, restore_at, restoration_id));
      }
      // Get the restoration state. Construct if in initial phase.
      RestorationState* restoration_ptr;
      if (phase == RestorePhase::kInitial) {
        LOG(INFO) << "Creating a new restoration entry with id " << restoration_id;
        // We mark sys catalog restoration phase to be complete in case of restorations
        // from external backups where we don't need to restore the sys catalog.
        auto restoration = std::make_unique<RestorationState>(
            &context_, restoration_id, &snapshot, restore_at,
            (snapshot.schedule_id().IsNil() ? IsSysCatalogRestored::kTrue :
                                              IsSysCatalogRestored::kFalse),
            GetRpcLimit(FLAGS_max_concurrent_restoration_rpcs,
                        FLAGS_max_concurrent_restoration_rpcs_per_tserver, leader_term));
        restoration_ptr = restorations_.emplace(std::move(restoration)).first->get();
        last_restorations_update_ht_ = context_.Clock()->Now();
      } else {
        restoration_ptr = &VERIFY_RESULT(FindRestoration(restoration_id)).get();
      }

      if (!restore_sys_catalog) {
        // Persist and replicate restoration entry to the sys catalog.
        // This ensures that followers can continue RPCs if this leader fails.
        // For PITR restorations, it is persisted as part of RESTORE_SYS_CATALOG operation.
        if (phase == RestorePhase::kInitial) {
          // Update the current leader term so that snapshot coordinator can pick it up.
          restoration_ptr->SetLeaderTerm(leader_term);
          RETURN_NOT_OK(SubmitRestoreWrite(restoration_ptr, leader_term));
        }
        tablet_list_empty = restoration_ptr->Empty();
        auto tablet_ids = snapshot.tablet_ids();
        snapshot_tablets.insert(tablet_ids.begin(), tablet_ids.end());
        restoration_ptr->PrepareOperations(&operations, snapshot_tablets, db_oid);
      }
    }

    // If sys catalog is restored, then tablets data will be restored after that using postponed
    // restores.
    if (restore_sys_catalog) {
      return SubmitRestore(snapshot_id, restore_at, restoration_id, leader_term);
    }

    ExecuteRestoreOperations(operations, leader_term);

    // For empty tablet list, finish the restore.
    if (tablet_list_empty) {
      std::lock_guard lock(mutex_);
      RestorationState* restoration_ptr = &VERIFY_RESULT(FindRestoration(restoration_id)).get();
      if (restoration_ptr) {
        FinishRestoration(restoration_ptr, leader_term);
      }
    }

    return Status::OK();
  }

  // Computes the maximum outstanding Snapshot Create/Delete RPC
  // that is permitted. If total limit is specified then it is used otherwise
  // the value is computed by multiplying tserver count with the per tserver limit.
  uint64_t GetRpcLimit(int64_t total_limit, int64_t per_tserver_limit, int64_t leader_term) {
    // NO OP for followers.
    if (leader_term < 0) {
      return std::numeric_limits<int>::max();
    }
    // Should execute only for leaders.
    if (total_limit == 0) {
      return std::numeric_limits<int>::max();
    }
    if (total_limit > 0) {
      return total_limit;
    }
    auto num_result = context_.GetNumLiveTServersForActiveCluster();
    // The cluster config could be empty for e.g. if a restore is in progress
    // or e.g. if a new master leader is elected. This is a temporary intermediate
    // state (we have bigger problems if the cluster config remains empty forever).
    // We use the limit set per tserver as the overall limit in such cases.
    if (!num_result.ok()) {
      LOG(INFO) << "Cluster Config is not available. Using per tserver limit of "
                << per_tserver_limit << " as the throttled limit.";
      return per_tserver_limit;
    }
    uint64_t num_tservers = *num_result;
    return num_tservers * per_tserver_limit;
  }

  SnapshotCoordinatorContext& context_;
  CatalogManager* cm_;
  std::mutex mutex_;
  class ScheduleTag;
  using Snapshots = boost::multi_index_container<
      std::unique_ptr<SnapshotState>,
      boost::multi_index::indexed_by<
          // Access snapshots by id.
          boost::multi_index::hashed_unique<
              boost::multi_index::const_mem_fun<
                  SnapshotState, const TxnSnapshotId&, &SnapshotState::id>
          >,
          // Group snapshots by schedule id. Ordered by hybrid time for the same schedule.
          boost::multi_index::ordered_non_unique<
              boost::multi_index::tag<ScheduleTag>,
              boost::multi_index::composite_key<
                  SnapshotState,
                  boost::multi_index::const_mem_fun<
                      SnapshotState, const SnapshotScheduleId&, &SnapshotState::schedule_id>,
                  boost::multi_index::const_mem_fun<
                      SnapshotState, HybridTime, &SnapshotState::snapshot_hybrid_time>
              >
          >
      >
  >;
  // For restorations and schedules we have to use multi_index since there are template
  // functions that expect same interface for those collections.
  using Restorations = boost::multi_index_container<
      std::unique_ptr<RestorationState>,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              boost::multi_index::const_mem_fun<
                  RestorationState, const TxnSnapshotRestorationId&,
                  &RestorationState::restoration_id>
          >,
          boost::multi_index::hashed_non_unique<
              boost::multi_index::tag<ScheduleTag>,
              boost::multi_index::const_mem_fun<
                  RestorationState, const SnapshotScheduleId&,
                  &RestorationState::schedule_id>
          >
      >
  >;
  using Schedules = boost::multi_index_container<
      std::unique_ptr<SnapshotScheduleState>,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              boost::multi_index::const_mem_fun<
                  SnapshotScheduleState, const SnapshotScheduleId&, &SnapshotScheduleState::id>
          >
      >
  >;

  Snapshots snapshots_ GUARDED_BY(mutex_);
  Restorations restorations_ GUARDED_BY(mutex_);
  HybridTime last_restorations_update_ht_ GUARDED_BY(mutex_);
  Schedules schedules_ GUARDED_BY(mutex_);
  rpc::Poller poller_;
};

MasterSnapshotCoordinator::MasterSnapshotCoordinator(
    SnapshotCoordinatorContext* context, CatalogManager* cm)
    : impl_(new Impl(context, cm)) {}

MasterSnapshotCoordinator::~MasterSnapshotCoordinator() {}

Result<TxnSnapshotId> MasterSnapshotCoordinator::Create(
    const SysRowEntries& entries, bool imported, int64_t leader_term, CoarseTimePoint deadline) {
  return impl_->Create(entries, imported, leader_term, deadline);
}

Status MasterSnapshotCoordinator::CreateReplicated(
    int64_t leader_term, const tablet::SnapshotOperation& operation) {
  return impl_->CreateReplicated(leader_term, operation);
}

Status MasterSnapshotCoordinator::DeleteReplicated(
    int64_t leader_term, const tablet::SnapshotOperation& operation) {
  return impl_->DeleteReplicated(leader_term, operation);
}

Status MasterSnapshotCoordinator::RestoreSysCatalogReplicated(
    int64_t leader_term, const tablet::SnapshotOperation& operation, Status* complete_status) {
  return impl_->RestoreSysCatalogReplicated(leader_term, operation, complete_status);
}

Status MasterSnapshotCoordinator::ListSnapshots(
    const TxnSnapshotId& snapshot_id, bool list_deleted,
    ListSnapshotsDetailOptionsPB options, ListSnapshotsResponsePB* resp) {
  return impl_->ListSnapshots(snapshot_id, list_deleted, options, resp);
}

Status MasterSnapshotCoordinator::Delete(
    const TxnSnapshotId& snapshot_id, int64_t leader_term, CoarseTimePoint deadline) {
  return impl_->Delete(snapshot_id, leader_term, deadline);
}

Status MasterSnapshotCoordinator::AbortRestore(const TxnSnapshotRestorationId& restoration_id,
    int64_t leader_term, CoarseTimePoint deadline) {
  return impl_->AbortRestore(restoration_id, leader_term, deadline);
}

Result<TxnSnapshotRestorationId> MasterSnapshotCoordinator::Restore(
    const TxnSnapshotId& snapshot_id, HybridTime restore_at, int64_t leader_term) {
  return impl_->Restore(snapshot_id, restore_at, leader_term);
}

Status MasterSnapshotCoordinator::ListRestorations(
    const TxnSnapshotRestorationId& restoration_id, const TxnSnapshotId& snapshot_id,
    ListSnapshotRestorationsResponsePB* resp) {
  return impl_->ListRestorations(restoration_id, snapshot_id, resp);
}

Result<SnapshotScheduleId> MasterSnapshotCoordinator::CreateSchedule(
    const CreateSnapshotScheduleRequestPB& request, int64_t leader_term,
    CoarseTimePoint deadline) {
  return impl_->CreateSchedule(request, leader_term, deadline);
}

Status MasterSnapshotCoordinator::ListSnapshotSchedules(
    const SnapshotScheduleId& snapshot_schedule_id, ListSnapshotSchedulesResponsePB* resp) {
  return impl_->ListSnapshotSchedules(snapshot_schedule_id, resp);
}

Status MasterSnapshotCoordinator::DeleteSnapshotSchedule(
    const SnapshotScheduleId& snapshot_schedule_id, int64_t leader_term, CoarseTimePoint deadline) {
  return impl_->DeleteSnapshotSchedule(snapshot_schedule_id, leader_term, deadline);
}

Result<SnapshotScheduleInfoPB> MasterSnapshotCoordinator::EditSnapshotSchedule(
    const SnapshotScheduleId& id,
    const EditSnapshotScheduleRequestPB& req,
    int64_t leader_term, CoarseTimePoint deadline) {
  return impl_->EditSnapshotSchedule(id, req, leader_term, deadline);
}

Status MasterSnapshotCoordinator::RestoreSnapshotSchedule(
    const SnapshotScheduleId& schedule_id, HybridTime restore_at,
    RestoreSnapshotScheduleResponsePB* resp, int64_t leader_term, CoarseTimePoint deadline) {
  return impl_->RestoreSnapshotSchedule(schedule_id, restore_at, resp, leader_term, deadline);
}

Status MasterSnapshotCoordinator::Load(tablet::Tablet* tablet) {
  return impl_->Load(tablet);
}

void MasterSnapshotCoordinator::Start() {
  impl_->Start();
}

void MasterSnapshotCoordinator::Shutdown() {
  impl_->Shutdown();
}

Status MasterSnapshotCoordinator::ApplyWritePair(const Slice& key, const Slice& value) {
  return impl_->ApplyWritePair(key, value);
}

Status MasterSnapshotCoordinator::FillHeartbeatResponse(TSHeartbeatResponsePB* resp) {
  return impl_->FillHeartbeatResponse(resp);
}

docdb::HistoryCutoff MasterSnapshotCoordinator::AllowedHistoryCutoffProvider(
    tablet::RaftGroupMetadata* metadata) {
  return impl_->AllowedHistoryCutoffProvider(metadata);
}

Result<SnapshotSchedulesToObjectIdsMap>
    MasterSnapshotCoordinator::MakeSnapshotSchedulesToObjectIdsMap(SysRowEntryType type) {
  return impl_->MakeSnapshotSchedulesToObjectIdsMap(type);
}

Result<bool> MasterSnapshotCoordinator::IsTableCoveredBySomeSnapshotSchedule(
    const TableInfo& table_info) {
  return impl_->IsTableCoveredBySomeSnapshotSchedule(table_info);
}

Result<bool> MasterSnapshotCoordinator::IsTableUndergoingPitrRestore(
    const TableInfo& table_info) {
  return impl_->IsTableUndergoingPitrRestore(table_info);
}

void MasterSnapshotCoordinator::SysCatalogLoaded(int64_t leader_term) {
  impl_->SysCatalogLoaded(leader_term);
}

Result<TxnSnapshotId> MasterSnapshotCoordinator::CreateForSchedule(
    const SnapshotScheduleId& schedule_id, int64_t leader_term, CoarseTimePoint deadline) {
  return impl_->CreateForSchedule(schedule_id, leader_term, deadline);
}

Result<docdb::KeyValuePairPB> MasterSnapshotCoordinator::UpdateRestorationAndGetWritePair(
    SnapshotScheduleRestoration* restoration) {
  return impl_->UpdateRestorationAndGetWritePair(restoration);
}

bool MasterSnapshotCoordinator::IsPitrActive() {
  return impl_->IsPitrActive();
}

} // namespace master
} // namespace yb
