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

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/util/opid.h"
#include "yb/util/metrics.h"

DEFINE_int32(retryable_request_timeout_secs, 300,
             "Amount of time to keep write request in index, to prevent duplicate writes.");

// We use this limit to prevent request range from infinite grow, because it will block log
// cleanup. I.e. even we have continous request range, it will be split by blocks, that could be
// dropped independently.
DEFINE_int32(retryable_request_range_time_limit_secs, 30,
             "Max delta in time for single op id range.");

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
  yb::OpId op_id;
  RestartSafeCoarseTimePoint time;
  mutable std::vector<ConsensusRoundPtr> duplicate_rounds;

  RunningRetryableRequest(
      RetryableRequestId request_id_, const OpId& op_id_, RestartSafeCoarseTimePoint time_)
      : request_id(request_id_), op_id(yb::OpId::FromPB(op_id_)), time(time_) {}
};

struct ReplicatedRetryableRequestRange {
  mutable RetryableRequestId first_id;
  RetryableRequestId last_id;
  yb::OpId min_op_id;
  mutable RestartSafeCoarseTimePoint min_time;
  mutable RestartSafeCoarseTimePoint max_time;

  ReplicatedRetryableRequestRange(RetryableRequestId id, const yb::OpId& op_id,
                              RestartSafeCoarseTimePoint time)
      : first_id(id), last_id(id), min_op_id(op_id), min_time(time),
        max_time(time) {}

  void InsertTime(const RestartSafeCoarseTimePoint& time) const {
    min_time = std::min(min_time, time);
    max_time = std::max(max_time, time);
  }

  void PrepareJoinWithPrev(const ReplicatedRetryableRequestRange& prev) const {
    min_time = std::min(min_time, prev.min_time);
    max_time = std::max(max_time, prev.max_time);
    first_id = prev.first_id;
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
        >,
        boost::multi_index::ordered_unique <
            boost::multi_index::tag<OpIdIndex>,
            boost::multi_index::member <
                RunningRetryableRequest, yb::OpId, &RunningRetryableRequest::op_id
            >
        >
    >
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
    >
> ReplicatedRetryableRequestRanges;

typedef ReplicatedRetryableRequestRanges::index<LastIdIndex>::type
    ReplicatedRetryableRequestRangesByLastId;

struct ClientRetryableRequests {
  RunningRetryableRequests running;
  ReplicatedRetryableRequestRanges replicated;
  RetryableRequestId min_running_request_id = 0;
  RestartSafeCoarseTimePoint empty_since;
};

std::chrono::seconds RangeTimeLimit() {
  return std::chrono::seconds(FLAGS_retryable_request_range_time_limit_secs);
}

} // namespace

class RetryableRequests::Impl {
 public:
  bool ShouldReplicateRound(const ConsensusRoundPtr& round) {
    if (!round->replicate_msg()->has_write_request()) {
      return true;
    }

    const auto& write_request = round->replicate_msg()->write_request();
    ClientId client_id(write_request.client_id1(), write_request.client_id2());

    if (client_id.IsNil()) {
      return true;
    }

    auto client_it = clients_.find(client_id);
    if (client_it == clients_.end()) {
      return true;
    }

    ClientRetryableRequests& client_write_requests = client_it->second;

    auto& replicated_indexed_by_last_id = client_write_requests.replicated.get<LastIdIndex>();
    if (write_request.min_running_request_id() > client_write_requests.min_running_request_id) {
      // We are not interested in ids below write_request.min_running_request_id() anymore.
      //
      // Request id intervals are ordered by last id of interval, and does not overlap.
      // So we are trying to find interval with last_id >= min_running_request_id
      // and trim it if necessary.
      auto it = replicated_indexed_by_last_id.lower_bound(write_request.min_running_request_id());
      if (it != replicated_indexed_by_last_id.end() &&
          it->first_id < write_request.min_running_request_id()) {
        it->first_id = write_request.min_running_request_id();
      }
      if (replicated_request_ranges_gauge_) {
        replicated_request_ranges_gauge_->DecrementBy(
            std::distance(replicated_indexed_by_last_id.begin(), it));
      }
      // Remove all intervals that has ids below write_request.min_running_request_id().
      replicated_indexed_by_last_id.erase(replicated_indexed_by_last_id.begin(), it);
      client_write_requests.min_running_request_id = write_request.min_running_request_id();
    }

    if (write_request.request_id() < client_write_requests.min_running_request_id) {
      round->NotifyReplicationFinished(
          STATUS(Expired, "Request id is below than min running"), round->bound_term());
      return false;
    }

    auto& running_indexed_by_request_id = client_write_requests.running.get<RequestIdIndex>();
    auto running_it = running_indexed_by_request_id.find(write_request.request_id());
    if (running_it != running_indexed_by_request_id.end()) {
      running_it->duplicate_rounds.push_back(round);
      return false;
    }

    auto it = replicated_indexed_by_last_id.lower_bound(write_request.request_id());
    if (it != replicated_indexed_by_last_id.end() && it->first_id <= write_request.request_id()) {
      round->NotifyReplicationFinished(
          STATUS(AlreadyPresent, "Duplicate write"), round->bound_term());
      return false;
    }

    return true;
  }

  void Register(const ReplicateMsg& replicate_msg, RestartSafeCoarseTimePoint entry_time) {
    const auto& write_request = replicate_msg.write_request();
    ClientId client_id(write_request.client_id1(), write_request.client_id2());

    if (client_id.IsNil()) {
      return;
    }

    if (entry_time == RestartSafeCoarseTimePoint()) {
      entry_time = clock_.Now();
    }
    clients_[client_id].running.emplace(write_request.request_id(), replicate_msg.id(), entry_time);
    VLOG(4) << "Running added " << client_id << "/" << write_request.request_id();
    if (running_requests_gauge_) {
      running_requests_gauge_->Increment();
    }
  }

  yb::OpId CleanExpiredReplicatedAndGetMinOpId() {
    yb::OpId result(std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max());
    auto now = clock_.Now();
    auto clean_start = now - std::chrono::seconds(FLAGS_retryable_request_timeout_secs);
    for (auto ci = clients_.begin(); ci != clients_.end();) {
      ClientRetryableRequests& client_write_requests = ci->second;
      auto& op_id_index = client_write_requests.replicated.get<OpIdIndex>();
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
      if (op_id_index.empty() && client_write_requests.running.empty()) {
        // We delay deleting client with empty requests, to be able to filter requests with too
        // small request id.
        if (client_write_requests.empty_since == RestartSafeCoarseTimePoint()) {
          client_write_requests.empty_since = now;
        } else if (client_write_requests.empty_since < clean_start) {
          ci = clients_.erase(ci);
          continue;
        }
      }
      ++ci;
    }

    return result;
  }

  void ReplicationFinished(
      const ReplicateMsg& replicate_msg, const Status& status, int64_t leader_term) {
    if (!replicate_msg.has_write_request()) {
      return;
    }

    const auto& write_request = replicate_msg.write_request();
    ClientId client_id(write_request.client_id1(), write_request.client_id2());

    if (client_id.IsNil()) {
      return;
    }

    auto& client = clients_[client_id];
    auto& running_indexed_by_request_id = client.running.get<RequestIdIndex>();
    auto running_it = running_indexed_by_request_id.find(write_request.request_id());
    if (running_it == running_indexed_by_request_id.end()) {
      LOG(DFATAL) << "Replication finished for request with unknown id "
                  << client_id << "/" << write_request.request_id() << ": "
                  << write_request.ShortDebugString();
      return;
    }
    auto status_for_duplicate = status.ok() ? STATUS(AlreadyPresent, "Duplicate write") : status;
    for (const auto& duplicate : running_it->duplicate_rounds) {
      duplicate->NotifyReplicationFinished(status_for_duplicate, leader_term);
    }
    RestartSafeCoarseTimePoint time = running_it->time;
    VLOG(4) << "Running replicated " << client_id << "/" << write_request.request_id();
    running_indexed_by_request_id.erase(running_it);
    if (running_requests_gauge_) {
      running_requests_gauge_->Decrement();
    }

    AddReplicated(yb::OpId::FromPB(replicate_msg.id()), write_request.request_id(), time, &client);
  }

  void MarkReplicatedUpTo(const yb::OpId& op_id) {
    for (auto& client_id_and_requests : clients_) {
      MarkReplicatedUpTo(op_id, client_id_and_requests.first, &client_id_and_requests.second);
    }
  }

  RestartSafeCoarseMonoClock& Clock() {
    return clock_;
  }

  void SetMetricEntity(const scoped_refptr<MetricEntity>& metric_entity) {
    running_requests_gauge_ = METRIC_running_retryable_requests.Instantiate(metric_entity, 0);
    replicated_request_ranges_gauge_ = METRIC_replicated_retryable_request_ranges.Instantiate(
        metric_entity, 0);
  }

  RetryableRequestsCounts TEST_Counts() {
    RetryableRequestsCounts result;
    for (const auto& p : clients_) {
      result.running += p.second.running.size();
      result.replicated += p.second.replicated.size();
    }
    return result;
  }

 private:
  void MarkReplicatedUpTo(
      const yb::OpId& op_id, const ClientId& client_id, ClientRetryableRequests* client) {
    auto& running_indexed_by_op_id = client->running.get<OpIdIndex>();
    auto it = running_indexed_by_op_id.begin();
    int64_t count = 0;
    for (; it != running_indexed_by_op_id.end() && it->op_id <= op_id; ++it) {
      AddReplicated(it->op_id, it->request_id, it->time, client);
      ++count;
      VLOG(4) << "Running marked " << client_id << "/" << it->request_id;
    }
    running_indexed_by_op_id.erase(running_indexed_by_op_id.begin(), it);
    if (running_requests_gauge_) {
      running_requests_gauge_->DecrementBy(count);
    }
  }

  void AddReplicated(yb::OpId op_id, RetryableRequestId request_id, RestartSafeCoarseTimePoint time,
                     ClientRetryableRequests* client) {
    auto& replicated_indexed_by_last_id = client->replicated.get<LastIdIndex>();
    auto request_it = replicated_indexed_by_last_id.lower_bound(request_id);
    // Points to the last range whose last id is strictly less than request_id.

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

    client->replicated.emplace(request_id, op_id, time);
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

  std::unordered_map<ClientId, ClientRetryableRequests, ClientIdHash> clients_;
  RestartSafeCoarseMonoClock clock_;
  scoped_refptr<AtomicGauge<int64_t>> running_requests_gauge_;
  scoped_refptr<AtomicGauge<int64_t>> replicated_request_ranges_gauge_;
};

RetryableRequests::RetryableRequests() : impl_(new Impl) {
}

RetryableRequests::~RetryableRequests() {
}

RetryableRequests::RetryableRequests(RetryableRequests&& rhs) : impl_(std::move(rhs.impl_)) {}

void RetryableRequests::operator=(RetryableRequests&& rhs) {
  impl_ = std::move(rhs.impl_);
}

bool RetryableRequests::ShouldReplicateRound(const ConsensusRoundPtr& round) {
  return impl_->ShouldReplicateRound(round);
}

void RetryableRequests::Register(
    const ReplicateMsg& replicate_msg, RestartSafeCoarseTimePoint entry_time) {
  impl_->Register(replicate_msg, entry_time);
}

yb::OpId RetryableRequests::CleanExpiredReplicatedAndGetMinOpId() {
  return impl_->CleanExpiredReplicatedAndGetMinOpId();
}

void RetryableRequests::ReplicationFinished(
    const ReplicateMsg& replicate_msg, const Status& status, int64_t leader_term) {
  impl_->ReplicationFinished(replicate_msg, status, leader_term);
}

void RetryableRequests::MarkReplicatedUpTo(const yb::OpId& op_id) {
  impl_->MarkReplicatedUpTo(op_id);
}

RestartSafeCoarseMonoClock& RetryableRequests::Clock() {
  return impl_->Clock();
}

RetryableRequestsCounts RetryableRequests::TEST_Counts() {
  return impl_->TEST_Counts();
}

void RetryableRequests::SetMetricEntity(const scoped_refptr<MetricEntity>& metric_entity) {
  impl_->SetMetricEntity(metric_entity);
}

} // namespace consensus
} // namespace yb
