// Copyright (c) YugabyteDB, Inc.

#include "yb/tserver/snapshot_preflush.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/service_util.h"
#include "yb/tserver/tablet_flusher.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/status_format.h"
#include "yb/util/sync_point.h"
#include "yb/util/threadpool.h"

using namespace std::literals;

DEFINE_RUNTIME_bool(snapshot_create_flush_before_submit, false,
    "Wait for separate flush RPCs to all captured replicas before submitting snapshot creation. "
    "An unavailable replica prevents snapshot submission; normal replication continues.");
TAG_FLAG(snapshot_create_flush_before_submit, advanced);
DEFINE_RUNTIME_int32(snapshot_preflush_timeout_ms, 10000,
    "Preflight flush budget, capped by the snapshot RPC deadline.");
DEFINE_validator(snapshot_preflush_timeout_ms, FLAG_GT_VALUE_VALIDATOR(0));
TAG_FLAG(snapshot_preflush_timeout_ms, advanced);
DEFINE_NON_RUNTIME_int32(snapshot_preflush_concurrency, 4,
    "Maximum outstanding snapshot preflights per tablet server, including retiring flush work.");
DEFINE_validator(snapshot_preflush_concurrency, FLAG_GT_VALUE_VALIDATOR(0));
TAG_FLAG(snapshot_preflush_concurrency, advanced);

METRIC_DEFINE_event_stats(server, snapshot_preflush_duration_us, "Snapshot preflight duration",
    yb::MetricUnit::kMicroseconds, "Time spent preflushing before snapshot submission");
METRIC_DEFINE_gauge_uint64(server, snapshot_preflush_active, "Active snapshot preflights",
    yb::MetricUnit::kOperations, "Admitted preflights including retiring work", 0);
METRIC_DEFINE_counter(server, snapshot_preflush_failures, "Snapshot preflight failures",
    yb::MetricUnit::kOperations, "Failed snapshot preflight attempts");
METRIC_DEFINE_counter(server, snapshot_preflush_timeouts, "Snapshot preflight timeouts",
    yb::MetricUnit::kOperations, "Snapshot preflight attempts that exhausted their deadline");
METRIC_DEFINE_counter(server, snapshot_preflush_rejections, "Snapshot preflight rejections",
    yb::MetricUnit::kOperations, "Snapshot preflight attempts rejected by admission control");

namespace yb::tserver {
namespace {

struct FlushResults;

struct Admission {
  // Weak entries let late RPC/local completions retain admission without retaining the service.
  std::mutex mutex;
  bool closing GUARDED_BY(mutex) = false;
  std::unordered_map<TabletId, std::weak_ptr<FlushResults>> active GUARDED_BY(mutex);
  scoped_refptr<AtomicGauge<uint64_t>> metric;
};

struct FlushResults {
  std::shared_ptr<Admission> admission;
  TabletId tablet_id;
  bool admitted = false;
  std::mutex mutex;
  std::condition_variable changed;
  size_t remaining GUARDED_BY(mutex) = 0;
  Status status GUARDED_BY(mutex);

  ~FlushResults() {
    if (admitted) {
      std::lock_guard lock(admission->mutex);
      admission->active.erase(tablet_id);
      admission->metric->Decrement();
    }
  }

  void Complete(const Status& result) {
    std::lock_guard lock(mutex);
    --remaining;
    if (status.ok() && !result.ok()) {
      status = result;
    }
    changed.notify_all();
  }

  void Stop() {
    std::lock_guard lock(mutex);
    status = STATUS(ShutdownInProgress, "Snapshot preflight is shutting down");
    changed.notify_all();
  }

  // Clang does not track std::unique_lock across condition-variable waits.
  Status Wait(CoarseTimePoint deadline) NO_THREAD_SAFETY_ANALYSIS {
    std::unique_lock lock(mutex);
    if (!changed.wait_until(lock, deadline, [this]() REQUIRES(mutex) {
          return !status.ok() || remaining == 0;
        })) {
      return STATUS(TimedOut, "Snapshot preflight flush deadline expired");
    }
    return status;
  }
};

struct RemoteFlush {
  RemoteFlush(rpc::ProxyCache* cache, const HostPort& address)
      : proxy(cache, address) {}
  TabletServerAdminServiceProxy proxy;
  FlushTabletsRequestPB request;
  FlushTabletsResponsePB response;
  rpc::RpcController controller;
};

// The operation's RPC callback owns its response storage. Release history before tablet refs.
struct Attempt {
  LeaderTabletPeer tablet;
  std::unique_ptr<tablet::SnapshotOperation> operation;
  tablet::ScopedReadOperation read_operation;
  std::shared_ptr<FlushResults> results;
  CoarseTimePoint deadline;
  std::optional<consensus::ConsensusStatePB> configuration;
};

using Membership = std::map<std::string, consensus::PeerMemberType>;

Membership Members(const consensus::RaftConfigPB& config) {
  Membership result;
  for (const auto& peer : config.peers()) {
    result.emplace(peer.permanent_uuid(), peer.member_type());
  }
  return result;
}

}  // namespace

class SnapshotPreflush::Impl {
 public:
  explicit Impl(TabletServer* server)
      : server_(*server), limit_(FLAGS_snapshot_preflush_concurrency),
        admission_(std::make_shared<Admission>()),
        duration_(METRIC_snapshot_preflush_duration_us.Instantiate(server->metric_entity())),
        failures_(METRIC_snapshot_preflush_failures.Instantiate(server->metric_entity())),
        timeouts_(METRIC_snapshot_preflush_timeouts.Instantiate(server->metric_entity())),
        rejections_(METRIC_snapshot_preflush_rejections.Instantiate(server->metric_entity())) {
    admission_->metric = METRIC_snapshot_preflush_active.Instantiate(server->metric_entity(), 0);
    CHECK_OK(ThreadPoolBuilder("snapshot-preflight").set_max_threads(static_cast<int>(limit_))
        .Build(&pool_));
  }

  void Submit(std::shared_ptr<Attempt> attempt) {
    auto results = std::make_shared<FlushResults>();
    results->admission = admission_;
    results->tablet_id = attempt->tablet.tablet->tablet_id();
    attempt->results = results;
    auto status = AdmitAndSubmit(attempt);
    if (!status.ok()) {
      rejections_->Increment();
      attempt->read_operation.Reset();
      attempt->operation->Aborted(status, false);
    }
  }

  void StartShutdown() {
    std::vector<std::shared_ptr<FlushResults>> active;
    {
      std::lock_guard lock(admission_->mutex);
      admission_->closing = true;
      for (const auto& [id, weak] : admission_->active) {
        if (auto results = weak.lock()) {
          active.push_back(std::move(results));
        }
      }
    }
    for (const auto& results : active) {
      results->Stop();
    }
  }

  void CompleteShutdown() {
    pool_->Wait();
    pool_->Shutdown();
  }

 private:
  Status AdmitAndSubmit(const std::shared_ptr<Attempt>& attempt) {
    const auto& results = attempt->results;
    std::lock_guard lock(admission_->mutex);
    SCHECK(!admission_->closing, ShutdownInProgress, "Snapshot preflight is shutting down");
    SCHECK_LT(admission_->active.size(), limit_, ServiceUnavailable,
              "Snapshot preflight capacity exhausted");
    SCHECK(!admission_->active.contains(results->tablet_id), ServiceUnavailable,
            "Snapshot preflight already in progress for tablet");
    admission_->active.emplace(results->tablet_id, results);
    results->admitted = true;
    admission_->metric->Increment();
    // Enqueue before StartShutdown can begin draining the pool.
    return pool_->SubmitFunc([this, attempt] { Run(attempt); });
  }

  void Run(const std::shared_ptr<Attempt>& attempt) {
    const auto start = CoarseMonoClock::Now();
    auto status = Preflush(attempt);
    if (status.ok()) {
      TEST_SYNC_POINT_CALLBACK("SnapshotPreflush::BeforeSubmit", attempt->tablet.tablet.get());
      // Recheck after hooks and all metadata reads, not only before the flush.
      status = CheckActive(*attempt);
    }
    duration_->Increment(ToMicroseconds(CoarseMonoClock::Now() - start));
    if (!status.ok()) {
      failures_->Increment();
      if (status.IsTimedOut()) {
        timeouts_->Increment();
      }
      LOG(WARNING) << "Snapshot preflight for " << attempt->results->tablet_id << ": " << status;
      attempt->operation->Aborted(status, false);
      return;
    }
    attempt->tablet.peer->Submit(std::move(attempt->operation), attempt->tablet.leader_term);
  }

  Status CheckActive(const Attempt& attempt) {
    {
      std::lock_guard lock(admission_->mutex);
      SCHECK(!admission_->closing, ShutdownInProgress, "Snapshot preflight is shutting down");
    }
    SCHECK_EQ(VERIFY_RESULT(LeaderTerm(*attempt.tablet.peer)), attempt.tablet.leader_term,
              IllegalState, "Snapshot preflight leader term changed");
    if (attempt.configuration) {
      auto consensus = VERIFY_RESULT(attempt.tablet.peer->GetConsensus());
      const auto current = consensus->ConsensusState(consensus::CONSENSUS_CONFIG_ACTIVE);
      SCHECK(current.current_term() == attempt.configuration->current_term() &&
             Members(current.config()) == Members(attempt.configuration->config()), TryAgain,
             "Snapshot preflight configuration changed");
    }
    SCHECK(CoarseMonoClock::Now() < attempt.deadline, TimedOut,
            "Snapshot preflight deadline expired before submission");
    return Status::OK();
  }

  Result<std::vector<std::shared_ptr<RemoteFlush>>> PrepareCalls(
      const Attempt& attempt, const consensus::RaftConfigPB& config) {
    std::vector<std::shared_ptr<RemoteFlush>> calls;
    calls.reserve(config.peers_size());
    std::unordered_set<std::string> seen;
    for (const auto& peer : config.peers()) {
      SCHECK(!peer.permanent_uuid().empty(), IllegalState, "Replica has no UUID");
      if (peer.permanent_uuid() == server_.permanent_uuid() ||
          !seen.insert(peer.permanent_uuid()).second) {
        continue;
      }
      SCHECK(!peer.last_known_private_addr().empty() || !peer.last_known_broadcast_addr().empty(),
              IllegalState, "Replica has no address");
      auto call = std::make_shared<RemoteFlush>(
          &server_.proxy_cache(), HostPortFromPB(DesiredHostPort(peer, server_.MakeCloudInfoPB())));
      call->request.set_dest_uuid(peer.permanent_uuid());
      call->request.add_tablet_ids(attempt.results->tablet_id);
      call->request.set_operation(FlushTabletsRequestPB::FLUSH);
      call->request.set_flags(tablet::FLUSH_COMPACT_ALL);
      call->request.set_propagated_hybrid_time(server_.Clock()->Now().ToUint64());
      call->controller.set_deadline(attempt.deadline);
      calls.push_back(std::move(call));
    }
    return calls;
  }

  Status Preflush(const std::shared_ptr<Attempt>& attempt) {
    RETURN_NOT_OK(CheckActive(*attempt));
    auto consensus = VERIFY_RESULT(attempt->tablet.peer->GetConsensus());
    attempt->configuration = consensus->ConsensusState(consensus::CONSENSUS_CONFIG_ACTIVE);
    const auto& state = *attempt->configuration;
    const auto membership = Members(state.config());
    SCHECK(membership.contains(server_.permanent_uuid()), IllegalState,
            "Snapshot preflight configuration omits the leader");
    auto calls = VERIFY_RESULT(PrepareCalls(*attempt, state.config()));
    auto results = attempt->results;
    {
      std::lock_guard lock(results->mutex);
      results->remaining = calls.size() + 1;
    }
    for (const auto& call : calls) {
      call->proxy.FlushTabletsAsync(call->request, &call->response, &call->controller,
          [call, results] {
            auto status = call->controller.status();
            if (status.ok() && call->response.has_error()) {
              status = StatusFromPB(call->response.error().status());
            }
            // A missing follower is not a missing leader tablet (terminal at the master).
            if (!status.ok()) {
              auto message = Format("Snapshot preflush on replica $0 failed: $1",
                                    call->request.dest_uuid(), status);
              status = status.IsTimedOut() ? STATUS(TimedOut, message) : STATUS(TryAgain, message);
            }
            results->Complete(status);
          });
    }
    FlushTabletsRequestPB request;
    request.set_operation(FlushTabletsRequestPB::FLUSH);
    request.set_flags(tablet::FLUSH_COMPACT_ALL);
    auto status = server_.tablet_manager()->tablet_flusher().Submit(
        {attempt->tablet.tablet}, request, attempt->deadline,
        [results](const Status& status, const TabletId&) { results->Complete(status); });
    if (!status.ok()) {
      results->Complete(status);
    }
    return results->Wait(attempt->deadline);
  }

  TabletServer& server_;
  const size_t limit_;
  std::shared_ptr<Admission> admission_;
  std::unique_ptr<ThreadPool> pool_;
  scoped_refptr<EventStats> duration_;
  scoped_refptr<Counter> failures_;
  scoped_refptr<Counter> timeouts_;
  scoped_refptr<Counter> rejections_;
};

SnapshotPreflush::SnapshotPreflush(TabletServer* server) : impl_(std::make_unique<Impl>(server)) {}

SnapshotPreflush::~SnapshotPreflush() {
  StartShutdown();
  CompleteShutdown();
}

void SnapshotPreflush::Submit(
    LeaderTabletPeer tablet, std::unique_ptr<tablet::SnapshotOperation> operation,
    tablet::ScopedReadOperation read_operation, CoarseTimePoint deadline) {
  auto attempt = std::make_shared<Attempt>();
  attempt->tablet = std::move(tablet);
  attempt->operation = std::move(operation);
  attempt->read_operation = std::move(read_operation);
  attempt->deadline = std::min(
      deadline, CoarseMonoClock::Now() + FLAGS_snapshot_preflush_timeout_ms * 1ms);
  impl_->Submit(std::move(attempt));
}

void SnapshotPreflush::StartShutdown() { impl_->StartShutdown(); }
void SnapshotPreflush::CompleteShutdown() { impl_->CompleteShutdown(); }

}  // namespace yb::tserver
