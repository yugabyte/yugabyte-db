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

#include "yb/consensus/retryable_requests.h"

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_round.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/opid_util.h"

#include "yb/server/clock.h"

#include "yb/tablet/operations.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/opid.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/rw_mutex.h"
#include "yb/util/status_format.h"

using namespace std::literals;

DEFINE_RUNTIME_int32(retryable_request_timeout_secs, 660,
    "Amount of time to keep write request in index, to prevent duplicate writes.");

// We use this limit to prevent request range from infinite grow, because it will block log
// cleanup. I.e. even we have continous request range, it will be split by blocks, that could be
// dropped independently.
DEFINE_UNKNOWN_int32(retryable_request_range_time_limit_secs, 30,
             "Max delta in time for single op id range.");

DEFINE_UNKNOWN_bool(enable_check_retryable_request_timeout, true,
                    "Whether to check if retryable request exceeds the timeout.");

DECLARE_uint64(max_clock_skew_usec);

METRIC_DEFINE_gauge_int64(tablet, running_retryable_requests,
                          "Number of running retryable requests.",
                          yb::MetricUnit::kRequests,
                          "Number of running retryable requests.");

METRIC_DEFINE_gauge_int64(tablet, replicated_retryable_request_ranges,
                          "Number of replicated retryable request ranges.",
                          yb::MetricUnit::kRequests,
                          "Number of replicated retryable request ranges.");

namespace yb {
namespace consensus {

namespace {

struct RunningRetryableRequest {
  RetryableRequestId request_id;
  RestartSafeCoarseTimePoint time;
  mutable std::vector<ConsensusRoundPtr> duplicate_rounds;

  RunningRetryableRequest(
      RetryableRequestId request_id_, RestartSafeCoarseTimePoint time_)
      : request_id(request_id_), time(time_) {}

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(request_id, time);
  }
};

struct ReplicatedRetryableRequestRange {
  mutable RetryableRequestId first_id;
  RetryableRequestId last_id;
  OpId min_op_id;
  mutable RestartSafeCoarseTimePoint min_time;
  mutable RestartSafeCoarseTimePoint max_time;

  ReplicatedRetryableRequestRange(RetryableRequestId id_,
                                  const OpId& op_id_,
                                  RestartSafeCoarseTimePoint time_)
      : first_id(id_), last_id(id_), min_op_id(op_id_), min_time(time_),
        max_time(time_) {}

  ReplicatedRetryableRequestRange(RetryableRequestId first_id_,
                                  RetryableRequestId last_id_,
                                  const OpId& min_op_id_,
                                  RestartSafeCoarseTimePoint min_time_,
                                  RestartSafeCoarseTimePoint max_time_)
      : first_id(first_id_), last_id(last_id_), min_op_id(min_op_id_), min_time(min_time_),
        max_time(max_time_) {}

  void InsertTime(const RestartSafeCoarseTimePoint& time) const {
    min_time = std::min(min_time, time);
    max_time = std::max(max_time, time);
  }

  void PrepareJoinWithPrev(const ReplicatedRetryableRequestRange& prev) const {
    min_time = std::min(min_time, prev.min_time);
    max_time = std::max(max_time, prev.max_time);
    first_id = prev.first_id;
  }

  std::string ToString() const {
    return Format("{ first_id: $0 last_id: $1 min_op_id: $2 min_time: $3 max_time: $4 }",
                  first_id, last_id, min_op_id, min_time, max_time);
  }
};

struct LastIdIndex;
struct OpIdIndex;
struct RequestIdIndex;

typedef boost::multi_index_container <
    RunningRetryableRequest,
    boost::multi_index::indexed_by <
        boost::multi_index::hashed_unique <
            boost::multi_index::tag<RequestIdIndex>,
            boost::multi_index::member <
                RunningRetryableRequest, RetryableRequestId, &RunningRetryableRequest::request_id
            >
        >
    >,
    MemTrackerAllocator<RunningRetryableRequest>
> RunningRetryableRequests;

typedef boost::multi_index_container <
    ReplicatedRetryableRequestRange,
    boost::multi_index::indexed_by <
        boost::multi_index::ordered_unique <
            boost::multi_index::tag<LastIdIndex>,
            boost::multi_index::member <
                ReplicatedRetryableRequestRange, RetryableRequestId,
                &ReplicatedRetryableRequestRange::last_id
            >
        >,
        boost::multi_index::ordered_unique <
            boost::multi_index::tag<OpIdIndex>,
            boost::multi_index::member <
                ReplicatedRetryableRequestRange, yb::OpId,
                &ReplicatedRetryableRequestRange::min_op_id
            >
        >
    >,
    MemTrackerAllocator<ReplicatedRetryableRequestRange>
> ReplicatedRetryableRequestRanges;

typedef ReplicatedRetryableRequestRanges::index<LastIdIndex>::type
    ReplicatedRetryableRequestRangesByLastId;

struct ClientRetryableRequests {
  std::unique_ptr<RunningRetryableRequests> running;
  std::unique_ptr<ReplicatedRetryableRequestRanges> replicated;
  RetryableRequestId min_running_request_id = 0;
  RestartSafeCoarseTimePoint empty_since;

  explicit ClientRetryableRequests(const MemTrackerPtr& mem_tracker)
      : running(std::make_unique<RunningRetryableRequests>(
            RunningRetryableRequests::ctor_args_list(),
            MemTrackerAllocator<RunningRetryableRequest>(mem_tracker))),
        replicated(std::make_unique<ReplicatedRetryableRequestRanges>(
            ReplicatedRetryableRequestRanges::ctor_args_list(),
            MemTrackerAllocator<ReplicatedRetryableRequestRange>(mem_tracker))) {
  }

  ClientRetryableRequests(const ClientRetryableRequests& c)
      : running(new RunningRetryableRequests(*c.running)),
        replicated(new ReplicatedRetryableRequestRanges(*c.replicated)),
        min_running_request_id(c.min_running_request_id),
        empty_since(c.empty_since) {
  }
};

std::chrono::seconds RangeTimeLimit() {
  return std::chrono::seconds(FLAGS_retryable_request_range_time_limit_secs);
}

class ReplicateData {
 public:
  ReplicateData() : client_id_(ClientId::Nil()), write_(nullptr) {}

  explicit ReplicateData(const tablet::LWWritePB* write, const LWOpIdPB& op_id)
      : client_id_(write->client_id1(), write->client_id2()),
        write_(write), op_id_(OpId::FromPB(op_id)) {
  }

  static ReplicateData FromMsg(const LWReplicateMsg& replicate_msg) {
    if (!replicate_msg.has_write()) {
      return ReplicateData();
    }

    return ReplicateData(&replicate_msg.write(), replicate_msg.id());
  }

  bool operator!() const {
    return client_id_.IsNil();
  }

  explicit operator bool() const {
    return !!*this;
  }

  const ClientId& client_id() const {
    return client_id_;
  }

  const tablet::LWWritePB& write() const {
    return *write_;
  }

  RetryableRequestId request_id() const {
    return write_->request_id();
  }

  const yb::OpId& op_id() const {
    return op_id_;
  }

 private:
  ClientId client_id_;
  const tablet::LWWritePB* write_;
  OpId op_id_;
};

std::ostream& operator<<(std::ostream& out, const ReplicateData& data) {
  return out << data.client_id() << '/' << data.request_id() << ": "
             << data.write().ShortDebugString() << " op_id: " << data.op_id();
}

} // namespace

Status RetryableRequestsManager::Init(const server::ClockPtr& clock) {
  retryable_requests_->SetServerClock(clock);
  if (!fs_manager_->Exists(dir_)) {
    LOG(INFO) << "Wal dir is not created, skip initializing RetryableRequestsManager for "
              << tablet_id_;
    // For first startup.
    has_file_on_disk_ = false;
    return Status::OK();
  }
  RETURN_NOT_OK(DoInit());
  LOG(INFO) << "Initialized RetryableRequestsManager, found a file ? "
            << (has_file_on_disk_ ? "yes" : "no")
            << ", wal dir=" << dir_;
  return Status::OK();
}

Status RetryableRequestsManager::SaveToDisk(std::unique_ptr<RetryableRequests> retryable_requests) {
  if (!retryable_requests) {
    return STATUS(IllegalState, "retryable_requests_copy_ is null,"
        "should set it before calling SaveToDisk");
  }
  RetryableRequestsPB pb;
  retryable_requests->ToPB(&pb);
  auto path = NewFilePath();
  LOG(INFO) << "Saving retryable requests up to " << pb.last_op_id() << " to " << path;
  auto* env = fs_manager()->env();
  RETURN_NOT_OK_PREPEND(pb_util::WritePBContainerToPath(
                            env, path, pb,
                            pb_util::OVERWRITE, pb_util::SYNC),
                            "Failed to write retryable requests to disk");
  // Delete the current file and rename new file to current file.
  if (has_file_on_disk_) {
    RETURN_NOT_OK(env->DeleteFile(CurrentFilePath()));
  }
  LOG(INFO) << "Renaming " << NewFileName() << " to " << FileName();
  RETURN_NOT_OK(env->RenameFile(NewFilePath(), CurrentFilePath()));
  has_file_on_disk_ = true;
  return env->SyncDir(dir_);
}

Status RetryableRequestsManager::LoadFromDisk() {
  if (!has_file_on_disk_) {
    return STATUS(NotFound, "Retryable requests has not been flushed");
  }
  RetryableRequestsPB pb;
  auto path = CurrentFilePath();
  RETURN_NOT_OK_PREPEND(
      pb_util::ReadPBContainerFromPath(fs_manager()->env(), path, &pb),
      Format("Could not load retryable requests from $0", path));
  retryable_requests_->FromPB(pb);
  LOG(INFO) << Format("Loaded tablet ($0) retryable requests "
                      "(max_replicated_op_id_=$1) from $2",
                      tablet_id_, pb.last_op_id(), path);
  return Status::OK();
}

Status RetryableRequestsManager::CopyTo(const std::string& dest_path) {
  if (!has_file_on_disk_) {
    return STATUS_FORMAT(NotFound, "Retryable requests has not been flushed");
  }
  auto* env = fs_manager()->env();
  auto path = CurrentFilePath();
  auto dest_path_tmp = pb_util::MakeTempPath(dest_path);
  LOG(INFO) << "Copying retryable requests from " << path << " to " << dest_path;
  DCHECK(fs_manager()->Exists(path));

  WritableFileOptions options;
  options.sync_on_close = true;
  RETURN_NOT_OK(env_util::CopyFile(
      fs_manager()->env(), path, dest_path_tmp, options));
  RETURN_NOT_OK(env->RenameFile(dest_path_tmp, dest_path));
  return env->SyncDir(dir_);
}

std::unique_ptr<RetryableRequests> RetryableRequestsManager::TakeSnapshotOfRetryableRequests() {
  if (!HasUnflushedData()) {
    // Simply return false if no new data to flush.
    YB_LOG_EVERY_N_SECS(INFO, 60)
        << "Tablet " << tablet_id_ << " has no new retryable requests to flush";
    return nullptr;
  }
  return std::make_unique<RetryableRequests>(*retryable_requests_);
}

Status RetryableRequestsManager::DoInit() {
  auto* env = fs_manager_->env();
  // Do cleanup - dlete temp new file if it exists.
  auto temp_file_path = pb_util::MakeTempPath(NewFilePath());
  if (env->FileExists(temp_file_path)) {
    RETURN_NOT_OK(env->DeleteFile(temp_file_path));
  }
  bool has_current = env->FileExists(CurrentFilePath());
  bool has_new = env->FileExists(NewFilePath());
  if (has_new) {
    // Should always load from the new file if it exists.
    if (has_current) {
      // If the current file exists, should delete it and rename the
      // new file to current file.
      RETURN_NOT_OK(env->DeleteFile(CurrentFilePath()));
    }
    RETURN_NOT_OK(env->RenameFile(NewFilePath(), CurrentFilePath()));
  }
  has_file_on_disk_ = has_new || has_current;
  return env->SyncDir(dir_);
}

class RetryableRequests::Impl {
 public:
  explicit Impl(const MemTrackerPtr& tablet_mem_tracker, std::string log_prefix)
      : log_prefix_(std::move(log_prefix)),
        mem_tracker_(MemTracker::FindOrCreateTracker(
            "Retryable Requests", tablet_mem_tracker)) {
    VLOG_WITH_PREFIX(1) << "Start";
  }

  Impl(const Impl& rhs) {
    CopyFrom(rhs);
  }

  void CopyFrom(const Impl& rhs) {
    log_prefix_ = rhs.log_prefix_;
    max_replicated_op_id_ = rhs.max_replicated_op_id_;
    last_flushed_op_id_ = rhs.last_flushed_op_id_;
    clock_ = rhs.clock_;
    running_requests_gauge_ = rhs.running_requests_gauge_;
    replicated_request_ranges_gauge_ = rhs.replicated_request_ranges_gauge_;

    for (const auto& rhs_client_requests : rhs.clients_) {
      ClientId client_id = rhs_client_requests.first;
      if (clients_.find(client_id) == clients_.end()) {
        clients_.emplace(client_id, ClientRetryableRequests(
            (mem_tracker_) ? mem_tracker_ : rhs.mem_tracker_));
      }
      auto& client_requests = clients_.at(client_id);

      auto& replicated_requests = client_requests.replicated;
      for (auto& rhs_replicated_request : *rhs_client_requests.second.replicated) {
        replicated_requests->emplace(rhs_replicated_request);
      }
      auto& running_requests = client_requests.running;
      for (auto& rhs_running_request : *rhs_client_requests.second.running) {
        running_requests->emplace(rhs_running_request);
      }
    }
  }

  bool HasUnflushedData() const {
    return max_replicated_op_id_ != last_flushed_op_id_;
  }

  void set_log_prefix(const std::string& log_prefix) {
    log_prefix_ = log_prefix;
  }

  OpId GetMaxReplicatedOpId() const {
    return max_replicated_op_id_;
  }

  void SetLastFlushedOpId(const OpId& op_id) {
    DCHECK_GE(op_id, last_flushed_op_id_);
    last_flushed_op_id_ = op_id;
  }

  OpId GetLastFlushedOpId() const {
    return last_flushed_op_id_;
  }

  void ToPB(RetryableRequestsPB* pb) const {
    max_replicated_op_id_.ToPB(pb->mutable_last_op_id());
    for (const auto& client_requests : clients_) {
      auto* client_requests_pb = pb->add_client_requests();
      auto pair = client_requests.first.ToUInt64Pair();
      client_requests_pb->set_client_id1(pair.first);
      client_requests_pb->set_client_id2(pair.second);
      VLOG_WITH_PREFIX(4) << Format("Saving $0 ranges for client $1",
          client_requests.second.replicated->size(), client_requests.first);
      for (const auto& range : *client_requests.second.replicated) {
        auto* range_pb = client_requests_pb->add_range();
        range_pb->set_first_id(range.first_id);
        range_pb->set_last_id(range.last_id);
        *range_pb->mutable_min_op_id() = MakeOpIdPB(range.min_op_id);
        range_pb->set_min_time(range.min_time.ToUInt64());
        range_pb->set_max_time(range.max_time.ToUInt64());
      }
    }
  }

  void FromPB(const RetryableRequestsPB& pb) {
    max_replicated_op_id_ = last_flushed_op_id_ = OpId::FromPB(pb.last_op_id());
    for (auto& reqs : pb.client_requests()) {
      ClientId client_id(reqs.client_id1(), reqs.client_id2());
     auto& client_requests = clients_.try_emplace(client_id, mem_tracker_).first->second;
      auto& replicated_requests = client_requests.replicated;
      VLOG_WITH_PREFIX(4) << Format("Loaded $0 ranges for client $1:\n$2",
          reqs.range_size(), client_id, reqs.DebugString());
      for (auto& r : reqs.range()) {
        replicated_requests->emplace(r.first_id(), r.last_id(),
                                     OpId::FromPB(r.min_op_id()),
                                     RestartSafeCoarseTimePoint::FromUInt64(r.min_time()),
                                     RestartSafeCoarseTimePoint::FromUInt64(r.max_time()));
        if (replicated_request_ranges_gauge_) {
          replicated_request_ranges_gauge_->Increment();
        }
      }
    }
  }

  Result<bool> Register(
      const ConsensusRoundPtr& round,
      tablet::IsLeaderSide is_leader_side,
      RestartSafeCoarseTimePoint entry_time) {
    auto data = ReplicateData::FromMsg(*round->replicate_msg());
    if (!data) {
      return true;
    }

    if (entry_time == RestartSafeCoarseTimePoint()) {
      entry_time = clock_.Now();
    }

    auto& client_retryable_requests = clients_.try_emplace(
        data.client_id(), mem_tracker_).first->second;

    CleanupReplicatedRequests(
        data.write().min_running_request_id(), &client_retryable_requests);

    if (data.request_id() < client_retryable_requests.min_running_request_id) {
      return STATUS_EC_FORMAT(
          Expired, MinRunningRequestIdStatusData(client_retryable_requests.min_running_request_id),
          "Request id $0 from client $1 is less than min running $2", data.request_id(),
          data.client_id(), client_retryable_requests.min_running_request_id);
    }

    auto& replicated_indexed_by_last_id = client_retryable_requests.replicated->get<LastIdIndex>();
    auto it = replicated_indexed_by_last_id.lower_bound(data.request_id());
    if (it != replicated_indexed_by_last_id.end() && it->first_id <= data.request_id()) {
      return STATUS_FORMAT(
              AlreadyPresent, "Duplicate request $0 from client $1 (min running $2)",
              data.request_id(), data.client_id(),
              client_retryable_requests.min_running_request_id);
    }

    // If there's start_time specified, check if the request is too old.
    // This should only be checked from the leader side.
    if (is_leader_side &&
        FLAGS_enable_check_retryable_request_timeout &&
        server_clock_ &&
        data.write().start_time_micros() > 0) {
      const auto retryable_request_timeout =
          GetAtomicFlag(&FLAGS_retryable_request_timeout_secs) * 1s;
      const auto max_clock_skew = FLAGS_max_clock_skew_usec * 1us;
      if (PREDICT_TRUE(retryable_request_timeout > max_clock_skew)) {
        const auto now_micros = server_clock_->Now().GetPhysicalValueMicros();
        VLOG_WITH_PREFIX(4) << Format(
            "Checking start_time(now:$0 start:$1)", now_micros, data.write().start_time_micros());
        if (data.write().start_time_micros() * 1us <
                now_micros * 1us - retryable_request_timeout + max_clock_skew) {
          return STATUS_EC_FORMAT(
              Expired,
              MinRunningRequestIdStatusData(client_retryable_requests.min_running_request_id),
              "Request id $0 from client $1 is too old (now=$2, start_time=$3, request timeout $4, "
              "max clock skew $5)",
              data.request_id(), data.client_id(), now_micros, data.write().start_time_micros(),
              retryable_request_timeout, max_clock_skew);
        }
      }
    }

    auto& running_indexed_by_request_id = client_retryable_requests.running->get<RequestIdIndex>();
    auto emplace_result = running_indexed_by_request_id.emplace(data.request_id(), entry_time);
    if (!emplace_result.second) {
      emplace_result.first->duplicate_rounds.push_back(round);
      return false;
    }

    VLOG_WITH_PREFIX(4) << "Running added " << data;
    if (running_requests_gauge_) {
      running_requests_gauge_->Increment();
    }

    return true;
  }

  OpId CleanExpiredReplicatedAndGetMinOpId() {
    OpId result = OpId::Max();
    auto now = clock_.Now();
    auto clean_start = now - GetAtomicFlag(&FLAGS_retryable_request_timeout_secs) * 1s;
    for (auto ci = clients_.begin(); ci != clients_.end();) {
      ClientRetryableRequests& client_retryable_requests = ci->second;
      auto& op_id_index = client_retryable_requests.replicated->get<OpIdIndex>();
      auto it = op_id_index.begin();
      int64_t count = 0;
      while (it != op_id_index.end() && it->max_time < clean_start) {
        ++it;
        ++count;
      }
      if (replicated_request_ranges_gauge_) {
        replicated_request_ranges_gauge_->DecrementBy(count);
      }
      if (it != op_id_index.end()) {
        result = std::min(result, it->min_op_id);
        op_id_index.erase(op_id_index.begin(), it);
      } else {
        op_id_index.clear();
      }
      if (op_id_index.empty() && client_retryable_requests.running->empty()) {
        // We delay deleting client with empty requests, to be able to filter requests with too
        // small request id.
        if (client_retryable_requests.empty_since == RestartSafeCoarseTimePoint()) {
          client_retryable_requests.empty_since = now;
        } else if (client_retryable_requests.empty_since < clean_start) {
          ci = clients_.erase(ci);
          continue;
        }
      }
      ++ci;
    }

    return result;
  }

  void ReplicationFinished(
      const LWReplicateMsg& replicate_msg, const Status& status, int64_t leader_term) {
    auto data = ReplicateData::FromMsg(replicate_msg);
    if (!data) {
      return;
    }

    auto& client_retryable_requests = clients_.try_emplace(
        data.client_id(), mem_tracker_).first->second;
    auto& running_indexed_by_request_id = client_retryable_requests.running->get<RequestIdIndex>();
    auto running_it = running_indexed_by_request_id.find(data.request_id());
    if (running_it == running_indexed_by_request_id.end()) {
#ifndef NDEBUG
      LOG_WITH_PREFIX(ERROR) << "Running requests: "
                             << AsString(running_indexed_by_request_id);
#endif
      LOG_WITH_PREFIX(DFATAL) << "Replication finished for request with unknown id " << data;
      return;
    }
    VLOG_WITH_PREFIX(4) << "Running " << (status.ok() ? "replicated " : "aborted ") << data
                        << ", " << status;

    static Status duplicate_write_status = STATUS(AlreadyPresent, "Duplicate request");
    auto status_for_duplicate = status.ok() ? duplicate_write_status : status;
    for (const auto& duplicate : running_it->duplicate_rounds) {
      duplicate->NotifyReplicationFinished(status_for_duplicate, leader_term,
                                           nullptr /* applied_op_ids */);
    }
    auto entry_time = running_it->time;
    running_indexed_by_request_id.erase(running_it);
    if (running_requests_gauge_) {
      running_requests_gauge_->Decrement();
    }

    if (status.ok()) {
      AddReplicated(
          yb::OpId::FromPB(replicate_msg.id()), data, entry_time, &client_retryable_requests);
    }
  }

  void Bootstrap(
      const LWReplicateMsg& replicate_msg, RestartSafeCoarseTimePoint entry_time) {
    if (max_replicated_op_id_ >= OpId::FromPB(replicate_msg.id())) {
      // Skip ops that already in retryable requests structure.
      return;
    }
    auto data = ReplicateData::FromMsg(replicate_msg);
    if (!data) {
      return;
    }

    auto& client_retryable_requests = clients_.try_emplace(
        data.client_id(), mem_tracker_).first->second;
    auto& running_indexed_by_request_id = client_retryable_requests.running->get<RequestIdIndex>();
    if (running_indexed_by_request_id.count(data.request_id()) != 0) {
#ifndef NDEBUG
      LOG_WITH_PREFIX(ERROR) << "Running requests: "
                             << yb::ToString(running_indexed_by_request_id);
#endif
      LOG_WITH_PREFIX(DFATAL) << "Bootstrapped running request " << data;
      return;
    }
    VLOG_WITH_PREFIX(4) << "Bootstrapped " << data;

    CleanupReplicatedRequests(
       data.write().min_running_request_id(), &client_retryable_requests);

    AddReplicated(
        yb::OpId::FromPB(replicate_msg.id()), data, entry_time, &client_retryable_requests);
  }

  RestartSafeCoarseMonoClock& Clock() {
    return clock_;
  }

  void SetMetricEntity(const scoped_refptr<MetricEntity>& metric_entity) {
    RetryableRequestsCounts counts = Counts();
    running_requests_gauge_ = METRIC_running_retryable_requests.Instantiate(
        metric_entity, counts.running);
    replicated_request_ranges_gauge_ = METRIC_replicated_retryable_request_ranges.Instantiate(
        metric_entity, counts.replicated);
  }

  void SetServerClock(const server::ClockPtr& clock) {
    server_clock_ = clock;
  }

  RetryableRequestsCounts Counts() {
    RetryableRequestsCounts result;
    for (const auto& p : clients_) {
      result.running += p.second.running->size();
      result.replicated += p.second.replicated->size();
      LOG_WITH_PREFIX(INFO) << "Replicated: " << yb::ToString(p.second.replicated);
    }
    return result;
  }

  Result<RetryableRequestId> MinRunningRequestId(const ClientId& client_id) const {
    const auto it = clients_.find(client_id);
    if (it == clients_.end()) {
      return STATUS_FORMAT(NotFound, "Client requests data not found for client $0", client_id);
    }
    return it->second.min_running_request_id;
  }

 private:
  void CleanupReplicatedRequests(
      RetryableRequestId new_min_running_request_id,
      ClientRetryableRequests* client_retryable_requests) {
    auto& replicated_indexed_by_last_id = client_retryable_requests->replicated->get<LastIdIndex>();
    if (new_min_running_request_id > client_retryable_requests->min_running_request_id) {
      // We are not interested in ids below write_request.min_running_request_id() anymore.
      //
      // Request id intervals are ordered by last id of interval, and does not overlap.
      // So we are trying to find interval with last_id >= min_running_request_id
      // and trim it if necessary.
      auto it = replicated_indexed_by_last_id.lower_bound(new_min_running_request_id);
      if (it != replicated_indexed_by_last_id.end() &&
          it->first_id < new_min_running_request_id) {
        it->first_id = new_min_running_request_id;
      }
      if (replicated_request_ranges_gauge_) {
        replicated_request_ranges_gauge_->DecrementBy(
            std::distance(replicated_indexed_by_last_id.begin(), it));
      }
      // Remove all intervals that has ids below write_request.min_running_request_id().
      replicated_indexed_by_last_id.erase(replicated_indexed_by_last_id.begin(), it);
      client_retryable_requests->min_running_request_id = new_min_running_request_id;
    }
  }

  void AddReplicated(yb::OpId op_id, const ReplicateData& data, RestartSafeCoarseTimePoint time,
                     ClientRetryableRequests* client) {
    auto request_id = data.request_id();
    auto& replicated_indexed_by_last_id = client->replicated->get<LastIdIndex>();
    auto request_it = replicated_indexed_by_last_id.lower_bound(request_id);
    if (request_it != replicated_indexed_by_last_id.end() && request_it->first_id <= request_id) {
#ifndef NDEBUG
      LOG_WITH_PREFIX(ERROR)
          << "Replicated requests: " << yb::ToString(client->replicated);
#endif

      LOG_WITH_PREFIX(DFATAL) << "Request already replicated: " << data;
      return;
    }

    if (max_replicated_op_id_ < op_id) {
      VLOG_WITH_PREFIX(4) << "Setting max_replicated_op_id_ to " << op_id;
      max_replicated_op_id_ = op_id;
    }

    // Check that we have range right after this id, and we could extend it.
    // Requests rarely attaches to begin of interval, so we could don't check for
    // RangeTimeLimit() here.
    if (request_it != replicated_indexed_by_last_id.end() &&
        request_it->first_id == request_id + 1) {
      op_id = std::min(request_it->min_op_id, op_id);
      request_it->InsertTime(time);
      // If previous range is right before this id, then we could just join those ranges.
      if (!TryJoinRanges(request_it, op_id, &replicated_indexed_by_last_id)) {
        --(request_it->first_id);
        UpdateMinOpId(request_it, op_id, &replicated_indexed_by_last_id);
      }
      return;
    }

    if (TryJoinToEndOfRange(request_it, op_id, request_id, time, &replicated_indexed_by_last_id)) {
      return;
    }

    client->replicated->emplace(request_id, op_id, time);
    if (replicated_request_ranges_gauge_) {
      replicated_request_ranges_gauge_->Increment();
    }
  }

  void UpdateMinOpId(
      ReplicatedRetryableRequestRangesByLastId::iterator request_it,
      yb::OpId min_op_id,
      ReplicatedRetryableRequestRangesByLastId* replicated_indexed_by_last_id) {
    if (min_op_id < request_it->min_op_id) {
      replicated_indexed_by_last_id->modify(request_it, [min_op_id](auto& entry) { // NOLINT
        entry.min_op_id = min_op_id;
      });
    }
  }

  bool TryJoinRanges(
      ReplicatedRetryableRequestRangesByLastId::iterator request_it,
      yb::OpId min_op_id,
      ReplicatedRetryableRequestRangesByLastId* replicated_indexed_by_last_id) {
    if (request_it == replicated_indexed_by_last_id->begin()) {
      return false;
    }

    auto request_prev_it = request_it;
    --request_prev_it;

    // We could join ranges if there is exactly one id between them, and request with that id was
    // just replicated...
    if (request_prev_it->last_id + 2 != request_it->first_id) {
      return false;
    }

    // ...and time range will fit into limit.
    if (request_it->max_time > request_prev_it->min_time + RangeTimeLimit()) {
      return false;
    }

    min_op_id = std::min(min_op_id, request_prev_it->min_op_id);
    request_it->PrepareJoinWithPrev(*request_prev_it);
    replicated_indexed_by_last_id->erase(request_prev_it);
    if (replicated_request_ranges_gauge_) {
      replicated_request_ranges_gauge_->Decrement();
    }
    UpdateMinOpId(request_it, min_op_id, replicated_indexed_by_last_id);

    return true;
  }

  bool TryJoinToEndOfRange(
      ReplicatedRetryableRequestRangesByLastId::iterator request_it,
      yb::OpId op_id, RetryableRequestId request_id, RestartSafeCoarseTimePoint time,
      ReplicatedRetryableRequestRangesByLastId* replicated_indexed_by_last_id) {
    if (request_it == replicated_indexed_by_last_id->begin()) {
      return false;
    }

    --request_it;

    if (request_it->last_id + 1 != request_id) {
      return false;
    }

    // It is rare case when request is attaches to end of range, but his time is lower than
    // min_time. So we could avoid checking for the case when
    // time + RangeTimeLimit() > request_prev_it->max_time
    if (time > request_it->min_time + RangeTimeLimit()) {
      return false;
    }

    op_id = std::min(request_it->min_op_id, op_id);
    request_it->InsertTime(time);
    // Actually we should use the modify function on client.replicated, but since the order of
    // ranges should not be changed, we could update last_id directly.
    ++const_cast<ReplicatedRetryableRequestRange&>(*request_it).last_id;

    UpdateMinOpId(request_it, op_id, replicated_indexed_by_last_id);

    return true;
  }

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  std::string log_prefix_;
  OpId max_replicated_op_id_ = OpId::Min();
  OpId last_flushed_op_id_ = OpId::Min();
  std::unordered_map<ClientId, ClientRetryableRequests, ClientIdHash> clients_;
  RestartSafeCoarseMonoClock clock_;
  server::ClockPtr server_clock_;
  scoped_refptr<AtomicGauge<int64_t>> running_requests_gauge_;
  scoped_refptr<AtomicGauge<int64_t>> replicated_request_ranges_gauge_;
  MemTrackerPtr mem_tracker_;
};

RetryableRequests::RetryableRequests(const MemTrackerPtr& tablet_mem_tracker,
                                     std::string log_prefix)
    : impl_(new Impl(tablet_mem_tracker, std::move(log_prefix))) {
}

RetryableRequests::~RetryableRequests() {
}

RetryableRequests::RetryableRequests(const RetryableRequests& rhs)
    : impl_(new Impl(*rhs.impl_)) {
}

RetryableRequests::RetryableRequests(RetryableRequests&& rhs) : impl_(std::move(rhs.impl_)) {}

void RetryableRequests::CopyFrom(const RetryableRequests& rhs) {
  impl_->CopyFrom(*rhs.impl_);
}

void RetryableRequests::operator=(RetryableRequests&& rhs) {
  impl_ = std::move(rhs.impl_);
}

OpId RetryableRequests::GetMaxReplicatedOpId() const {
  return impl_->GetMaxReplicatedOpId();
}

void RetryableRequests::SetLastFlushedOpId(const OpId& op_id) {
  impl_->SetLastFlushedOpId(op_id);
}

OpId RetryableRequests::GetLastFlushedOpId() const {
  return impl_->GetLastFlushedOpId();
}

void RetryableRequests::ToPB(RetryableRequestsPB *pb) const {
  impl_->ToPB(pb);
}

void RetryableRequests::FromPB(const RetryableRequestsPB &pb) {
  impl_->FromPB(pb);
}

Result<bool> RetryableRequests::Register(
    const ConsensusRoundPtr& round,
    tablet::IsLeaderSide is_leader_side,
    RestartSafeCoarseTimePoint entry_time) {
  return impl_->Register(round, is_leader_side, entry_time);
}

yb::OpId RetryableRequests::CleanExpiredReplicatedAndGetMinOpId() {
  return impl_->CleanExpiredReplicatedAndGetMinOpId();
}

void RetryableRequests::ReplicationFinished(
    const LWReplicateMsg& replicate_msg, const Status& status, int64_t leader_term) {
  impl_->ReplicationFinished(replicate_msg, status, leader_term);
}

void RetryableRequests::Bootstrap(
    const LWReplicateMsg& replicate_msg, RestartSafeCoarseTimePoint entry_time) {
  impl_->Bootstrap(replicate_msg, entry_time);
}

RestartSafeCoarseMonoClock& RetryableRequests::Clock() {
  return impl_->Clock();
}

RetryableRequestsCounts RetryableRequests::TEST_Counts() {
  return impl_->Counts();
}

Result<RetryableRequestId> RetryableRequests::MinRunningRequestId(
    const ClientId& client_id) const {
  return impl_->MinRunningRequestId(client_id);
}

void RetryableRequests::SetMetricEntity(const scoped_refptr<MetricEntity>& metric_entity) {
  impl_->SetMetricEntity(metric_entity);
}

void RetryableRequests::set_log_prefix(const std::string& log_prefix) {
  impl_->set_log_prefix(log_prefix);
}

bool RetryableRequests::HasUnflushedData() const {
  return impl_->HasUnflushedData();
}

void RetryableRequests::SetServerClock(const server::ClockPtr& clock) {
  impl_->SetServerClock(clock);
}

} // namespace consensus
} // namespace yb
