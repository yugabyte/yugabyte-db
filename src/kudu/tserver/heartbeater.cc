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

#include "kudu/tserver/heartbeater.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <memory>
#include <vector>

#include "kudu/common/wire_protocol.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/master/master.h"
#include "kudu/master/master_rpc.h"
#include "kudu/master/master.proxy.h"
#include "kudu/server/webserver.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/tserver/tablet_server_options.h"
#include "kudu/tserver/ts_tablet_manager.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/thread.h"
#include "kudu/util/monotime.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/status.h"

DEFINE_int32(heartbeat_rpc_timeout_ms, 15000,
             "Timeout used for the TS->Master heartbeat RPCs.");
TAG_FLAG(heartbeat_rpc_timeout_ms, advanced);

DEFINE_int32(heartbeat_interval_ms, 1000,
             "Interval at which the TS heartbeats to the master.");
TAG_FLAG(heartbeat_interval_ms, advanced);

DEFINE_int32(heartbeat_max_failures_before_backoff, 3,
             "Maximum number of consecutive heartbeat failures until the "
             "Tablet Server backs off to the normal heartbeat interval, "
             "rather than retrying.");
TAG_FLAG(heartbeat_max_failures_before_backoff, advanced);

using google::protobuf::RepeatedPtrField;
using kudu::HostPortPB;
using kudu::consensus::RaftPeerPB;
using kudu::master::GetLeaderMasterRpc;
using kudu::master::ListMastersResponsePB;
using kudu::master::Master;
using kudu::master::MasterServiceProxy;
using kudu::rpc::RpcController;
using std::shared_ptr;
using strings::Substitute;

namespace kudu {
namespace tserver {

namespace {

// Creates a proxy to 'hostport'.
Status MasterServiceProxyForHostPort(const HostPort& hostport,
                                     const shared_ptr<rpc::Messenger>& messenger,
                                     gscoped_ptr<MasterServiceProxy>* proxy) {
  vector<Sockaddr> addrs;
  RETURN_NOT_OK(hostport.ResolveAddresses(&addrs));
  if (addrs.size() > 1) {
    LOG(WARNING) << "Master address '" << hostport.ToString() << "' "
                 << "resolves to " << addrs.size() << " different addresses. Using "
                 << addrs[0].ToString();
  }
  proxy->reset(new MasterServiceProxy(messenger, addrs[0]));
  return Status::OK();
}

} // anonymous namespace

// Most of the actual logic of the heartbeater is inside this inner class,
// to avoid having too many dependencies from the header itself.
//
// This is basically the "PIMPL" pattern.
class Heartbeater::Thread {
 public:
  Thread(const TabletServerOptions& opts, TabletServer* server);

  Status Start();
  Status Stop();
  void TriggerASAP();

 private:
  void RunThread();
  Status FindLeaderMaster(const MonoTime& deadline,
                          HostPort* leader_hostport);
  Status ConnectToMaster();
  int GetMinimumHeartbeatMillis() const;
  int GetMillisUntilNextHeartbeat() const;
  Status DoHeartbeat();
  Status SetupRegistration(master::TSRegistrationPB* reg);
  void SetupCommonField(master::TSToMasterCommonPB* common);
  bool IsCurrentThread() const;

  // The hosts/ports of masters that we may heartbeat to.
  //
  // We keep the HostPort around rather than a Sockaddr because the
  // masters may change IP addresses, and we'd like to re-resolve on
  // every new attempt at connecting.
  vector<HostPort> master_addrs_;

  // Index of the master we last succesfully obtained the master
  // consensus configuration information from.
  int last_locate_master_idx_;

  // The server for which we are heartbeating.
  TabletServer* const server_;

  // The actual running thread (NULL before it is started)
  scoped_refptr<kudu::Thread> thread_;

  // Host and port of the most recent leader master.
  HostPort leader_master_hostport_;

  // Current RPC proxy to the leader master.
  gscoped_ptr<master::MasterServiceProxy> proxy_;

  // The most recent response from a heartbeat.
  master::TSHeartbeatResponsePB last_hb_response_;

  // True once at least one heartbeat has been sent.
  bool has_heartbeated_;

  // The number of heartbeats which have failed in a row.
  // This is tracked so as to back-off heartbeating.
  int consecutive_failed_heartbeats_;

  // Mutex/condition pair to trigger the heartbeater thread
  // to either heartbeat early or exit.
  Mutex mutex_;
  ConditionVariable cond_;

  // Protected by mutex_.
  bool should_run_;
  bool heartbeat_asap_;

  DISALLOW_COPY_AND_ASSIGN(Thread);
};

////////////////////////////////////////////////////////////
// Heartbeater
////////////////////////////////////////////////////////////

Heartbeater::Heartbeater(const TabletServerOptions& opts, TabletServer* server)
  : thread_(new Thread(opts, server)) {
}
Heartbeater::~Heartbeater() {
  WARN_NOT_OK(Stop(), "Unable to stop heartbeater thread");
}

Status Heartbeater::Start() { return thread_->Start(); }
Status Heartbeater::Stop() { return thread_->Stop(); }
void Heartbeater::TriggerASAP() { thread_->TriggerASAP(); }

////////////////////////////////////////////////////////////
// Heartbeater::Thread
////////////////////////////////////////////////////////////

Heartbeater::Thread::Thread(const TabletServerOptions& opts, TabletServer* server)
  : master_addrs_(opts.master_addresses),
    last_locate_master_idx_(0),
    server_(server),
    has_heartbeated_(false),
    consecutive_failed_heartbeats_(0),
    cond_(&mutex_),
    should_run_(false),
    heartbeat_asap_(false) {
  CHECK(!master_addrs_.empty());
}

namespace {
void LeaderMasterCallback(HostPort* dst_hostport,
                          Synchronizer* sync,
                          const Status& status,
                          const HostPort& result) {
  if (status.ok()) {
    *dst_hostport = result;
  }
  sync->StatusCB(status);
}
} // anonymous namespace

Status Heartbeater::Thread::FindLeaderMaster(const MonoTime& deadline,
                                             HostPort* leader_hostport) {
  Status s = Status::OK();
  if (master_addrs_.size() == 1) {
    // "Shortcut" the process when a single master is specified.
    *leader_hostport = master_addrs_[0];
    return Status::OK();
  }
  vector<Sockaddr> master_sock_addrs;
  for (const HostPort& master_addr : master_addrs_) {
    vector<Sockaddr> addrs;
    Status s = master_addr.ResolveAddresses(&addrs);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to resolve address '" << master_addr.ToString()
                   << "': " << s.ToString();
      continue;
    }
    if (addrs.size() > 1) {
      LOG(WARNING) << "Master address '" << master_addr.ToString() << "' "
                   << "resolves to " << addrs.size() << " different addresses. Using "
                   << addrs[0].ToString();
    }
    master_sock_addrs.push_back(addrs[0]);
  }
  if (master_sock_addrs.empty()) {
    return Status::NotFound("unable to resolve any of the master addresses!");
  }
  Synchronizer sync;
  scoped_refptr<GetLeaderMasterRpc> rpc(new GetLeaderMasterRpc(
                                          Bind(&LeaderMasterCallback,
                                               leader_hostport,
                                               &sync),
                                          master_sock_addrs,
                                          deadline,
                                          server_->messenger()));
  rpc->SendRpc();
  return sync.Wait();
}

Status Heartbeater::Thread::ConnectToMaster() {
  vector<Sockaddr> addrs;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms));
  // TODO send heartbeats without tablet reports to non-leader masters.
  RETURN_NOT_OK(FindLeaderMaster(deadline, &leader_master_hostport_));
  gscoped_ptr<MasterServiceProxy> new_proxy;
  MasterServiceProxyForHostPort(leader_master_hostport_,
                                server_->messenger(),
                                &new_proxy);
  RETURN_NOT_OK(leader_master_hostport_.ResolveAddresses(&addrs));

  // Ping the master to verify that it's alive.
  master::PingRequestPB req;
  master::PingResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms));
  RETURN_NOT_OK_PREPEND(new_proxy->Ping(req, &resp, &rpc),
                        Substitute("Failed to ping master at $0", addrs[0].ToString()));
  LOG(INFO) << "Connected to a leader master server at " << leader_master_hostport_.ToString();
  proxy_.reset(new_proxy.release());
  return Status::OK();
}

void Heartbeater::Thread::SetupCommonField(master::TSToMasterCommonPB* common) {
  common->mutable_ts_instance()->CopyFrom(server_->instance_pb());
}

Status Heartbeater::Thread::SetupRegistration(master::TSRegistrationPB* reg) {
  reg->Clear();

  vector<Sockaddr> addrs;
  RETURN_NOT_OK(CHECK_NOTNULL(server_->rpc_server())->GetBoundAddresses(&addrs));
  RETURN_NOT_OK_PREPEND(AddHostPortPBs(addrs, reg->mutable_rpc_addresses()),
                        "Failed to add RPC addresses to registration");

  addrs.clear();
  RETURN_NOT_OK_PREPEND(CHECK_NOTNULL(server_->web_server())->GetBoundAddresses(&addrs),
                        "Unable to get bound HTTP addresses");
  RETURN_NOT_OK_PREPEND(AddHostPortPBs(addrs, reg->mutable_http_addresses()),
                        "Failed to add HTTP addresses to registration");
  return Status::OK();
}

int Heartbeater::Thread::GetMinimumHeartbeatMillis() const {
  // If we've failed a few heartbeats in a row, back off to the normal
  // interval, rather than retrying in a loop.
  if (consecutive_failed_heartbeats_ == FLAGS_heartbeat_max_failures_before_backoff) {
    LOG(WARNING) << "Failed " << consecutive_failed_heartbeats_  <<" heartbeats "
                 << "in a row: no longer allowing fast heartbeat attempts.";
  }

  return consecutive_failed_heartbeats_ > FLAGS_heartbeat_max_failures_before_backoff ?
    FLAGS_heartbeat_interval_ms : 0;
}

int Heartbeater::Thread::GetMillisUntilNextHeartbeat() const {
  // When we first start up, heartbeat immediately.
  if (!has_heartbeated_) {
    return GetMinimumHeartbeatMillis();
  }

  // If the master needs something from us, we should immediately
  // send another heartbeat with that info, rather than waiting for the interval.
  if (last_hb_response_.needs_reregister() ||
      last_hb_response_.needs_full_tablet_report()) {
    return GetMinimumHeartbeatMillis();
  }

  return FLAGS_heartbeat_interval_ms;
}

Status Heartbeater::Thread::DoHeartbeat() {
  if (PREDICT_FALSE(server_->fail_heartbeats_for_tests())) {
    return Status::IOError("failing all heartbeats for tests");
  }

  CHECK(IsCurrentThread());

  if (!proxy_) {
    VLOG(1) << "No valid master proxy. Connecting...";
    RETURN_NOT_OK(ConnectToMaster());
    DCHECK(proxy_);
  }

  master::TSHeartbeatRequestPB req;

  SetupCommonField(req.mutable_common());
  if (last_hb_response_.needs_reregister()) {
    LOG(INFO) << "Registering TS with master...";
    RETURN_NOT_OK_PREPEND(SetupRegistration(req.mutable_registration()),
                          "Unable to set up registration");
  }

  if (last_hb_response_.needs_full_tablet_report()) {
    LOG(INFO) << "Sending a full tablet report to master...";
    server_->tablet_manager()->GenerateFullTabletReport(
      req.mutable_tablet_report());
  } else {
    VLOG(2) << "Sending an incremental tablet report to master...";
    server_->tablet_manager()->GenerateIncrementalTabletReport(
      req.mutable_tablet_report());
  }
  req.set_num_live_tablets(server_->tablet_manager()->GetNumLiveTablets());

  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(10));

  VLOG(2) << "Sending heartbeat:\n" << req.DebugString();
  master::TSHeartbeatResponsePB resp;
  RETURN_NOT_OK_PREPEND(proxy_->TSHeartbeat(req, &resp, &rpc),
                        "Failed to send heartbeat");
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  VLOG(2) << "Received heartbeat response:\n" << resp.DebugString();
  if (!resp.leader_master()) {
    // If the master is no longer a leader, reset proxy so that we can
    // determine the master and attempt to heartbeat during in the
    // next heartbeat interval.
    proxy_.reset();
    return Status::ServiceUnavailable("master is no longer the leader");
  }
  last_hb_response_.Swap(&resp);


  // TODO: Handle TSHeartbeatResponsePB (e.g. deleted tablets and schema changes)
  server_->tablet_manager()->MarkTabletReportAcknowledged(req.tablet_report());

  return Status::OK();
}

void Heartbeater::Thread::RunThread() {
  CHECK(IsCurrentThread());
  VLOG(1) << "Heartbeat thread starting";

  // Set up a fake "last heartbeat response" which indicates that we
  // need to register -- since we've never registered before, we know
  // this to be true.  This avoids an extra
  // heartbeat/response/heartbeat cycle.
  last_hb_response_.set_needs_reregister(true);
  last_hb_response_.set_needs_full_tablet_report(true);

  while (true) {
    MonoTime next_heartbeat = MonoTime::Now(MonoTime::FINE);
    next_heartbeat.AddDelta(MonoDelta::FromMilliseconds(GetMillisUntilNextHeartbeat()));

    // Wait for either the heartbeat interval to elapse, or for an "ASAP" heartbeat,
    // or for the signal to shut down.
    {
      MutexLock l(mutex_);
      while (true) {
        MonoDelta remaining = next_heartbeat.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
        if (remaining.ToMilliseconds() <= 0 ||
            heartbeat_asap_ ||
            !should_run_) {
          break;
        }
        cond_.TimedWait(remaining);
      }

      heartbeat_asap_ = false;

      if (!should_run_) {
        VLOG(1) << "Heartbeat thread finished";
        return;
      }
    }

    Status s = DoHeartbeat();
    if (!s.ok()) {
      LOG(WARNING) << "Failed to heartbeat to " << leader_master_hostport_.ToString()
                   << ": " << s.ToString();
      consecutive_failed_heartbeats_++;
      if (master_addrs_.size() > 1) {
        // If we encountered a network error (e.g., connection
        // refused) and there's more than one master available, try
        // determining the leader master again.
        if (s.IsNetworkError() ||
            consecutive_failed_heartbeats_ == FLAGS_heartbeat_max_failures_before_backoff) {
          proxy_.reset();
        }
      }
      continue;
    }
    consecutive_failed_heartbeats_ = 0;
    has_heartbeated_ = true;
  }
}

bool Heartbeater::Thread::IsCurrentThread() const {
  return thread_.get() == kudu::Thread::current_thread();
}

Status Heartbeater::Thread::Start() {
  CHECK(thread_ == nullptr);

  should_run_ = true;
  return kudu::Thread::Create("heartbeater", "heartbeat",
      &Heartbeater::Thread::RunThread, this, &thread_);
}

Status Heartbeater::Thread::Stop() {
  if (!thread_) {
    return Status::OK();
  }

  {
    MutexLock l(mutex_);
    should_run_ = false;
    cond_.Signal();
  }
  RETURN_NOT_OK(ThreadJoiner(thread_.get()).Join());
  thread_ = nullptr;
  return Status::OK();
}

void Heartbeater::Thread::TriggerASAP() {
  MutexLock l(mutex_);
  heartbeat_asap_ = true;
  cond_.Signal();
}

} // namespace tserver
} // namespace kudu
