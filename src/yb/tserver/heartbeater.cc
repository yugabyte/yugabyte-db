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

#include "yb/tserver/heartbeater.h"

#include <memory>
#include <vector>
#include <mutex>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_rpc.h"
#include "yb/server/server_base.proxy.h"
#include "yb/server/webserver.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server_options.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/flag_tags.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"
#include "yb/util/mem_tracker.h"
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
using yb::HostPortPB;
using yb::consensus::RaftPeerPB;
using yb::master::GetLeaderMasterRpc;
using yb::master::ListMastersResponsePB;
using yb::master::Master;
using yb::master::MasterServiceProxy;
using yb::rpc::RpcController;
using std::shared_ptr;
using std::vector;
using strings::Substitute;

namespace yb {
namespace tserver {

namespace {

// Creates a proxy to 'hostport'.
Status MasterServiceProxyForHostPort(const HostPort& hostport,
                                     const shared_ptr<rpc::Messenger>& messenger,
                                     gscoped_ptr<MasterServiceProxy>* proxy) {
  std::vector<Endpoint> addrs;
  RETURN_NOT_OK(hostport.ResolveAddresses(&addrs));
  if (addrs.size() > 1) {
    LOG(WARNING) << "Master address '" << hostport.ToString() << "' "
                 << "resolves to " << addrs.size() << " different addresses. Using "
                 << yb::ToString(addrs[0]);
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

  void set_master_addresses(shared_ptr<const vector<HostPort>> master_addresses) {
    std::lock_guard<std::mutex> l(master_addresses_mtx_);
    master_addresses_ = std::move(master_addresses);
    VLOG(1) << "Setting master addresses to "
            << HostPort::ToCommaSeparatedString(*master_addresses_);
  }

 private:
  void RunThread();
  Status FindLeaderMaster(const MonoTime& deadline,
                          HostPort* leader_hostport);
  Status ConnectToMaster();
  int GetMinimumHeartbeatMillis() const;
  int GetMillisUntilNextHeartbeat() const;
  CHECKED_STATUS DoHeartbeat();
  CHECKED_STATUS TryHeartbeat();
  CHECKED_STATUS SetupRegistration(master::TSRegistrationPB* reg);
  void SetupCommonField(master::TSToMasterCommonPB* common);
  bool IsCurrentThread() const;

  shared_ptr<const vector<HostPort>> get_master_addresses() {
    std::lock_guard<std::mutex> l(master_addresses_mtx_);
    CHECK_NOTNULL(master_addresses_.get());
    return master_addresses_;
  }

  // Protecting master_addresses_.
  std::mutex master_addresses_mtx_;

  // The hosts/ports of masters that we may heartbeat to.
  //
  // We keep the HostPort around rather than a Sockaddr because the
  // masters may change IP addresses, and we'd like to re-resolve on
  // every new attempt at connecting.
  shared_ptr<const vector<HostPort>> master_addresses_;

  // Index of the master we last succesfully obtained the master
  // consensus configuration information from.
  int last_locate_master_idx_;

  // The server for which we are heartbeating.
  TabletServer* const server_;

  // The actual running thread (NULL before it is started)
  scoped_refptr<yb::Thread> thread_;

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

  // The interval for sending tserver metrics in the heartbeat.
  const int tserver_metrics_interval_sec_;
  // stores the granularity for updating file sizes and current read/write
  MonoTime prev_tserver_metrics_submission_;

  // Stores the total read and writes ops for computing iops
  uint64_t prev_reads_;
  uint64_t prev_writes_;

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
void Heartbeater::set_master_addresses(shared_ptr<const vector<HostPort>> master_addresses) {
  thread_->set_master_addresses(master_addresses);
}

////////////////////////////////////////////////////////////
// Heartbeater::Thread
////////////////////////////////////////////////////////////

Heartbeater::Thread::Thread(const TabletServerOptions& opts, TabletServer* server)
  : master_addresses_(opts.GetMasterAddresses()),
    last_locate_master_idx_(0),
    server_(server),
    has_heartbeated_(false),
    consecutive_failed_heartbeats_(0),
    cond_(&mutex_),
    should_run_(false),
    heartbeat_asap_(false),
    tserver_metrics_interval_sec_(5),
    prev_tserver_metrics_submission_(MonoTime::FineNow()),
    prev_reads_(0),
    prev_writes_(0) {
  CHECK_NOTNULL(master_addresses_.get());
  CHECK(!master_addresses_->empty());
  VLOG(1) << "Initializing heartbeater thread with master addresses: "
          << HostPort::ToCommaSeparatedString(*master_addresses_);
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
  const auto master_addresses = get_master_addresses();
  if (master_addresses->size() == 1) {
    // "Shortcut" the process when a single master is specified.
    *leader_hostport = (*master_addresses)[0];
    return Status::OK();
  }
  std::vector<Endpoint> master_sock_addrs;
  for (const HostPort& master_addr : *master_addresses) {
    std::vector<Endpoint> addrs;
    Status s = master_addr.ResolveAddresses(&addrs);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to resolve address '" << master_addr.ToString()
                   << "': " << s.ToString();
      continue;
    }
    if (addrs.size() > 1) {
      LOG(WARNING) << "Master address '" << master_addr.ToString() << "' "
                   << "resolves to " << addrs.size() << " different addresses. Using "
                   << addrs[0];
    }
    master_sock_addrs.push_back(addrs[0]);
  }
  if (master_sock_addrs.empty()) {
    return STATUS(NotFound, "unable to resolve any of the master addresses!");
  }
  rpc::Rpcs rpcs;
  Synchronizer sync;
  auto rpc = rpc::StartRpc<GetLeaderMasterRpc>(
      Bind(&LeaderMasterCallback, leader_hostport, &sync),
      master_sock_addrs,
      deadline,
      server_->messenger(),
      &rpcs);
  return sync.Wait();
}

Status Heartbeater::Thread::ConnectToMaster() {
  std::vector<Endpoint> addrs;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms));
  // TODO send heartbeats without tablet reports to non-leader masters.
  Status s = FindLeaderMaster(deadline, &leader_master_hostport_);
  if (!s.ok()) {
    LOG(INFO) << "Find leader master "  <<  leader_master_hostport_.ToString()
              << " hit error " << s.ToString();
    return s;
  }

  RETURN_NOT_OK(leader_master_hostport_.ResolveAddresses(&addrs));
  // Pings are common for both Master and Tserver.
  gscoped_ptr<server::GenericServiceProxy> new_proxy;
  new_proxy.reset(new server::GenericServiceProxy(server_->messenger(), addrs[0]));

  // Ping the master to verify that it's alive.
  server::PingRequestPB req;
  server::PingResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms));
  RETURN_NOT_OK_PREPEND(new_proxy->Ping(req, &resp, &rpc),
                        Substitute("Failed to ping master at $0", yb::ToString(addrs[0])));
  LOG(INFO) << "Connected to a leader master server at " << leader_master_hostport_.ToString();

  // Save state in the instance.
  MasterServiceProxyForHostPort(leader_master_hostport_, server_->messenger(), &proxy_);
  return Status::OK();
}

void Heartbeater::Thread::SetupCommonField(master::TSToMasterCommonPB* common) {
  common->mutable_ts_instance()->CopyFrom(server_->instance_pb());
}

Status Heartbeater::Thread::SetupRegistration(master::TSRegistrationPB* reg) {
  reg->Clear();
  RETURN_NOT_OK(server_->GetRegistration(reg->mutable_common()));

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

Status Heartbeater::Thread::TryHeartbeat() {
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


  if (prev_tserver_metrics_submission_ +
      MonoDelta::FromSeconds(tserver_metrics_interval_sec_) < MonoTime::FineNow()) {

    // Get the total memory used.
    rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == -1) {
      LOG(ERROR) << "Could not get the memory usage; getrusage() failed";
    } else {
      req.mutable_metrics()->set_total_ram_usage(ru.ru_maxrss);
      VLOG(4) << "Total Memory Usage: " << ru.ru_maxrss;
    }
    // Get the Total SST file sizes and set it in the proto buf
    std::vector<scoped_refptr<yb::tablet::TabletPeer> > tablet_peers;
    uint64_t total_file_sizes = 0;
    server_->tablet_manager()->GetTabletPeers(&tablet_peers);
    for (auto it = tablet_peers.begin(); it != tablet_peers.end(); it++) {
      scoped_refptr<yb::tablet::TabletPeer> tablet_peer = *it;
      if (tablet_peer) {
        shared_ptr<yb::tablet::TabletClass> tablet_class = tablet_peer->shared_tablet();
        total_file_sizes += (tablet_class) ? tablet_class->GetTotalSSTFileSizes() : 0;
      }
    }
    req.mutable_metrics()->set_total_sst_file_size(total_file_sizes);

    // Get the total number of read and write operations.
    scoped_refptr<Histogram> reads_hist = server_->GetMetricsHistogram
        (TabletServerServiceIf::RpcMetricIndexes::kMetricIndexRead);
    uint64_t  num_reads = (reads_hist != nullptr) ? reads_hist->TotalCount() : 0;

    scoped_refptr<Histogram> writes_hist = server_->GetMetricsHistogram
        (TabletServerServiceIf::RpcMetricIndexes::kMetricIndexWrite);
    uint64_t num_writes = (writes_hist != nullptr) ? writes_hist->TotalCount() : 0;

    // Calculate the read and write ops per second.
    MonoDelta diff = MonoTime::FineNow() - prev_tserver_metrics_submission_;
    double_t div = diff.ToSeconds();

    double rops_per_sec = (div > 0 && num_reads > 0) ?
        (static_cast<double>(num_reads - prev_reads_) / div) : 0;

    double wops_per_sec = (div > 0 && num_writes > 0) ?
        (static_cast<double>(num_writes - prev_writes_) / div) : 0;

    prev_reads_ = num_reads;
    prev_writes_ = num_writes;
    req.mutable_metrics()->set_read_ops_per_sec(rops_per_sec);
    req.mutable_metrics()->set_write_ops_per_sec(wops_per_sec);
    prev_tserver_metrics_submission_ = MonoTime::FineNow();

    VLOG(4) << "Read Ops per second: " << rops_per_sec;
    VLOG(4) << "Write Ops per second: " << wops_per_sec;
    VLOG(4) << "Total SST File Sizes: "<< total_file_sizes;
  }

  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(10));

  req.set_config_index(server_->GetCurrentMasterIndex());

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
    return STATUS(ServiceUnavailable, "master is no longer the leader");
  }
  last_hb_response_.Swap(&resp);
  if (last_hb_response_.needs_full_tablet_report()) {
    return STATUS(TryAgain, "");
  }

  if (resp.has_cluster_uuid() && !resp.cluster_uuid().empty()) {
    server_->set_cluster_uuid(resp.cluster_uuid());
  }

  if (resp.has_master_config()) {
    LOG(INFO) << "Received heartbeat response with config " << resp.DebugString();

    RETURN_NOT_OK(server_->UpdateMasterAddresses(resp.master_config()));
  }

  // TODO: Handle TSHeartbeatResponsePB (e.g. deleted tablets and schema changes)
  server_->tablet_manager()->MarkTabletReportAcknowledged(req.tablet_report());

  // Update the live tserver list.
  return server_->PopulateLiveTServers(resp);
}

Status Heartbeater::Thread::DoHeartbeat() {
  if (PREDICT_FALSE(server_->fail_heartbeats_for_tests())) {
    return STATUS(IOError, "failing all heartbeats for tests");
  }

  CHECK(IsCurrentThread());

  if (!proxy_) {
    VLOG(1) << "No valid master proxy. Connecting...";
    RETURN_NOT_OK(ConnectToMaster());
    DCHECK(proxy_);
  }

  for (;;) {
    auto status = TryHeartbeat();
    if (!status.ok() && status.IsTryAgain()) {
      continue;
    }
    return status;
  }

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
      const auto master_addresses = get_master_addresses();
      LOG(WARNING) << "Failed to heartbeat to " << leader_master_hostport_.ToString()
                   << ": " << s.ToString() << " tries=" << consecutive_failed_heartbeats_
                   << ", num=" << master_addresses->size()
                   << ", masters=" << HostPort::ToCommaSeparatedString(*master_addresses)
                   << ", code=" << s.CodeAsString();
      consecutive_failed_heartbeats_++;
      if (master_addresses->size() > 1) {
        // If we encountered a network error (e.g., connection
        // refused) or timed out and there's more than one master available, try
        // determining the leader master again.
        if (s.IsNetworkError() || s.IsTimedOut() ||
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
  return thread_.get() == yb::Thread::current_thread();
}

Status Heartbeater::Thread::Start() {
  CHECK(thread_ == nullptr);

  should_run_ = true;
  return yb::Thread::Create("heartbeater", "heartbeat",
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
} // namespace yb
