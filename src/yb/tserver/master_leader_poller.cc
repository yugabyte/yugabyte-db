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

#include "yb/tserver/master_leader_poller.h"

#include "yb/gutil/bind.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/master/master_rpc.h"

#include "yb/util/thread.h"
#include "yb/util/mutex.h"
#include "yb/util/async_util.h"

#include "yb/server/server_base.proxy.h"

using namespace std::literals;

DECLARE_int32(heartbeat_max_failures_before_backoff);

namespace yb::tserver {

class MasterLeaderPollScheduler::Impl {
 public:
  Impl(
    MasterLeaderFinder& finder, MasterLeaderPollerInterface& poller)
    : finder_(finder), poller_(poller), cond_(&mutex_) {}

  Status Start();
  void Shutdown();
  void Run();
  void TriggerASAP();

  rpc::ProxyCache& proxy_cache() {
    return finder_.get_proxy_cache();
  }

  void UpdateMasterAddresses(server::MasterAddressesPtr master_addresses) {
    finder_.set_master_addresses(std::move(master_addresses));
  }

 private:
  const std::string& LogPrefix() const;

  MasterLeaderFinder& finder_;
  MasterLeaderPollerInterface& poller_;
  scoped_refptr<Thread> thread_;
  bool should_run_ GUARDED_BY(mutex_) = false;
  int consecutive_failures_ = 0;
  Mutex mutex_;
  ConditionVariable cond_;
  bool poll_asap_ GUARDED_BY(mutex_) = false;
};

MasterLeaderPollScheduler::MasterLeaderPollScheduler(
    MasterLeaderFinder& finder, MasterLeaderPollerInterface& poller)
    : impl_(std::make_unique<MasterLeaderPollScheduler::Impl>(finder, poller)) {}

Status MasterLeaderPollScheduler::Start() { return impl_->Start(); }

void MasterLeaderPollScheduler::Shutdown() {
  impl_->Shutdown();
}

void MasterLeaderPollScheduler::TriggerASAP() { impl_->TriggerASAP(); }

MasterLeaderPollScheduler::~MasterLeaderPollScheduler() {
  Shutdown();
}

Status MasterLeaderPollScheduler::Impl::Start() {
  MutexLock l(mutex_);
  CHECK(thread_ == nullptr);
  should_run_ = true;
  return Thread::Create(
      "master_leader_poller", poller_.name(), &MasterLeaderPollScheduler::Impl::Run, this,
      &thread_);
}

void MasterLeaderPollScheduler::Impl::Shutdown() {
  {
    MutexLock l(mutex_);
    if (!thread_) {
      return;
    }
    poll_asap_ = true;
    should_run_ = false;
    YB_PROFILE(cond_.Signal());
  }
  finder_.Shutdown();

  WARN_NOT_OK(ThreadJoiner(thread_.get()).Join(), "Poll thread join failed");
  thread_ = nullptr;
}

void MasterLeaderPollScheduler::Impl::Run() {
  poller_.Init();

  for (;;) {
    auto delta = poller_.IntervalToNextPoll(consecutive_failures_);
    VLOG_IF(1, consecutive_failures_ > 0) << LogPrefix() << "Next poll in " << delta.ToString();
    auto next_poll = MonoTime::Now() + delta;

    {
      MutexLock l(mutex_);
      while (!poll_asap_ && cond_.WaitUntil(next_poll)) {}
      poll_asap_ = false;
      if (!should_run_) {
        VLOG_WITH_PREFIX(1) << "thread finished.";
        return;
      }
    }

    Status s = poller_.Poll();
    if (!s.ok()) {
      const auto master_addresses = finder_.get_master_addresses();
      LOG_WITH_PREFIX(WARNING) << "Failed to poll " << finder_.get_master_leader_hostport()
                               << ": " << s << " tries=" << consecutive_failures_
                               << ", num=" << master_addresses->size()
                               << ", masters=" << AsString(master_addresses)
                               << ", code=" << s.CodeAsString();
      consecutive_failures_++;
      // If there's multiple masters...
      if (master_addresses->size() > 1 || (*master_addresses)[0].size() > 1) {
        // If we encountered a network error (e.g., connection refused) or reached our failure
        // threshold, try determining the leader master again. Heartbeats function as a watchdog,
        // so timeouts should be considered normal failures.
        if (s.IsNetworkError() ||
            consecutive_failures_ >= FLAGS_heartbeat_max_failures_before_backoff) {
          poller_.ResetProxy();
        }
      }
      continue;
    }
    VLOG_IF(1, consecutive_failures_ > 0)
        << LogPrefix()
        << "Successfully polled master leader at : " << finder_.get_master_leader_hostport()
        << " after " << consecutive_failures_ << " failures.";
    consecutive_failures_ = 0;
  }
}

void MasterLeaderPollScheduler::Impl::TriggerASAP() {
  MutexLock l(mutex_);
  poll_asap_ = true;
  YB_PROFILE(cond_.Signal());
}

const std::string& MasterLeaderPollScheduler::Impl::LogPrefix() const {
  return poller_.LogPrefix();
}

void MasterLeaderPollScheduler::UpdateMasterAddresses(server::MasterAddressesPtr master_addresses) {
  impl_->UpdateMasterAddresses(std::move(master_addresses));
}

MasterLeaderFinder::MasterLeaderFinder(
    rpc::Messenger* messenger, rpc::ProxyCache& proxy_cache,
    server::MasterAddressesPtr master_addresses)
    : messenger_(messenger),
      proxy_cache_(proxy_cache),
      master_addresses_(std::move(master_addresses)) {}

namespace {
struct FindLeaderMasterData {
  HostPort result;
  Synchronizer sync;
  std::shared_ptr<master::GetLeaderMasterRpc> rpc;
};

void LeaderMasterCallback(const std::shared_ptr<FindLeaderMasterData>& data,
                          const Status& status,
                          const HostPort& result) {
  if (status.ok()) {
    data->result = result;
  }
  data->sync.StatusCB(status);
}

}  // anonymous namespace

Result<HostPort> MasterLeaderFinder::FindMasterLeader(MonoDelta timeout) {
  const auto master_addresses = get_master_addresses_unlocked();
  if (master_addresses->size() == 1 && (*master_addresses)[0].size() == 1) {
    return (*master_addresses)[0][0];
  }
  auto master_sock_addrs = *master_addresses;
  if (master_sock_addrs.empty()) {
    return STATUS(NotFound, "Unable to resolve any of the master addresses!");
  }

  auto data = std::make_shared<FindLeaderMasterData>();
  data->rpc = std::make_shared<master::GetLeaderMasterRpc>(
      Bind(&LeaderMasterCallback, data),
      master_sock_addrs,
      CoarseMonoClock::Now() + timeout,
      messenger_,
      &proxy_cache_,
      &rpcs_,
      true /* should_timeout_to_follower_ */);
  data->rpc->SendRpc();
  auto status = data->sync.WaitFor(timeout + 1s);
  if (status.ok()) {
    return data->result;
  }
  rpcs_.RequestAbortAll();
  return status.CloneAndPrepend(
      Format("Failed to find master leader using master addresses $0", *master_addresses));
}

Result<HostPort> MasterLeaderFinder::UpdateMasterLeaderHostPort(MonoDelta timeout) {
  // Keep a local copy of the latest leader master hostport we compute to avoid holding the lock
  // while making a synchronous RPC.
  HostPort local_hp_copy;
  {
    std::lock_guard l(master_meta_mtx_);
    master_leader_hostport_ =
        VERIFY_RESULT_PREPEND(FindMasterLeader(timeout), "Attempt to find leader master failed");
    local_hp_copy = master_leader_hostport_;
  }

  // Pings are common for both Master and Tserver.
  auto new_proxy =
      std::make_unique<server::GenericServiceProxy>(&proxy_cache_, local_hp_copy);

  // Ping the master to verify that it's alive.
  server::PingRequestPB req;
  server::PingResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(timeout);
  RETURN_NOT_OK_PREPEND(
      new_proxy->Ping(req, &resp, &rpc),
      Format("Failed to ping master at $0", local_hp_copy));
  VLOG(1) << "Connected to leader master server at " << local_hp_copy;
  return local_hp_copy;
}

rpc::ProxyCache& MasterLeaderFinder::get_proxy_cache() {
  return proxy_cache_;
}

server::MasterAddressesPtr MasterLeaderFinder::get_master_addresses_unlocked() const {
    CHECK_NOTNULL(master_addresses_.get());
    return master_addresses_;
}

server::MasterAddressesPtr MasterLeaderFinder::get_master_addresses() const {
    std::lock_guard l(master_meta_mtx_);
    return get_master_addresses_unlocked();
}

HostPort MasterLeaderFinder::get_master_leader_hostport() const {
  std::lock_guard l(master_meta_mtx_);
  return master_leader_hostport_;
}

void MasterLeaderFinder::set_master_addresses(server::MasterAddressesPtr master_addresses) {
  std::lock_guard l(master_meta_mtx_);
  master_addresses_ = std::move(master_addresses);
}

void MasterLeaderFinder::Shutdown() {
  rpcs_.Shutdown();
}

}  // namespace yb::tserver
