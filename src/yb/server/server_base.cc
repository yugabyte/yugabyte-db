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
#include "yb/server/server_base.h"

#include <boost/algorithm/string/predicate.hpp>
#include <gflags/gflags.h>
#include <string>
#include <vector>

#include "yb/common/wire_protocol.h"
#include "yb/fs/fs_manager.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/walltime.h"
#include "yb/rpc/messenger.h"
#include "yb/server/default-path-handlers.h"
#include "yb/server/generic_service.h"
#include "yb/server/glog_metrics.h"
#include "yb/server/hybrid_clock.h"
#include "yb/server/logical_clock.h"
#include "yb/server/rpc_server.h"
#include "yb/server/rpcz-path-handler.h"
#include "yb/server/server_base.pb.h"
#include "yb/server/server_base_options.h"
#include "yb/server/tcmalloc_metrics.h"
#include "yb/server/tracing-path-handlers.h"
#include "yb/server/webserver.h"
#include "yb/util/atomic.h"
#include "yb/util/env.h"
#include "yb/util/flag_tags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/pb_util.h"
#include "yb/util/rolling_log.h"
#include "yb/util/spinlock_profiling.h"
#include "yb/util/thread.h"
#include "yb/util/version_info.h"

DEFINE_int32(num_reactor_threads, 4, "Number of libev reactor threads to start.");
TAG_FLAG(num_reactor_threads, advanced);

DECLARE_bool(use_hybrid_clock);

using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using strings::Substitute;

namespace yb {
namespace server {

namespace {

// Disambiguates between servers when in a minicluster.
AtomicInt<int32_t> mem_tracker_id_counter(-1);

shared_ptr<MemTracker> CreateMemTrackerForServer() {
  int32_t id = mem_tracker_id_counter.Increment();
  string id_str = "server";
  if (id != 0) {
    StrAppend(&id_str, " ", id);
  }
  return shared_ptr<MemTracker>(MemTracker::CreateTracker(-1, id_str));
}

} // anonymous namespace

RpcServerBase::RpcServerBase(string name, const ServerBaseOptions& options,
                             const string& metric_namespace)
    : name_(std::move(name)),
      mem_tracker_(CreateMemTrackerForServer()),
      metric_registry_(new MetricRegistry()),
      metric_entity_(METRIC_ENTITY_server.Instantiate(metric_registry_.get(),
                                                      metric_namespace)),
      rpc_server_(new RpcServer(options.rpc_opts)),
      is_first_run_(false),
      connection_type_(options.connection_type),
      options_(options),
      initialized_(false),
      stop_metrics_logging_latch_(1) {
  if (FLAGS_use_hybrid_clock) {
    clock_ = new HybridClock();
  } else {
    clock_ = LogicalClock::CreateStartingAt(Timestamp::kInitialTimestamp);
  }
}

RpcServerBase::~RpcServerBase() {
  Shutdown();
  mem_tracker_->UnregisterFromParent();
}

Sockaddr RpcServerBase::first_rpc_address() const {
  vector<Sockaddr> addrs;
  WARN_NOT_OK(rpc_server_->GetBoundAddresses(&addrs),
              "Couldn't get bound RPC address");
  CHECK(!addrs.empty()) << "Not bound";
  return addrs[0];
}

const NodeInstancePB& RpcServerBase::instance_pb() const {
  return *DCHECK_NOTNULL(instance_pb_.get());
}

Status RpcServerBase::Init() {
  CHECK(!initialized_);

  glog_metrics_.reset(new ScopedGLogMetrics(metric_entity_));
  tcmalloc::RegisterMetrics(metric_entity_);
  RegisterSpinLockContentionMetrics(metric_entity_);

  InitSpinLockContentionProfiling();

  // Initialize the clock immediately. This checks that the clock is synchronized
  // so we're less likely to get into a partially initialized state on disk during startup
  // if we're having clock problems.
  RETURN_NOT_OK_PREPEND(clock_->Init(), "Cannot initialize clock");

  // Create the Messenger.
  rpc::MessengerBuilder builder(name_);

  builder.set_num_reactors(FLAGS_num_reactor_threads);
  builder.set_metric_entity(metric_entity());
  builder.use_connection_type(connection_type_);
  RETURN_NOT_OK(builder.Build(&messenger_));

  RETURN_NOT_OK(rpc_server_->Init(messenger_));
  RETURN_NOT_OK(rpc_server_->Bind());
  clock_->RegisterMetrics(metric_entity_);

  RETURN_NOT_OK_PREPEND(StartMetricsLogging(), "Could not enable metrics logging");

  initialized_ = true;
  return Status::OK();
}

std::string RpcServerBase::ToString() const {
  return strings::Substitute("$0 : rpc=$1", name_, first_rpc_address().ToString());
}

void RpcServerBase::GetStatusPB(ServerStatusPB* status) const {
  // Node instance
  status->mutable_node_instance()->CopyFrom(*instance_pb_);

  // RPC ports
  {
    vector<Sockaddr> addrs;
    CHECK_OK(rpc_server_->GetBoundAddresses(&addrs));
    for (const Sockaddr& addr : addrs) {
      HostPortPB* pb = status->add_bound_rpc_addresses();
      pb->set_host(addr.host());
      pb->set_port(addr.port());
    }
  }

  VersionInfo::GetVersionInfoPB(status->mutable_version_info());
}

Status RpcServerBase::DumpServerInfo(const string& path,
                                     const string& format) const {
  ServerStatusPB status;
  GetStatusPB(&status);

  if (boost::iequals(format, "json")) {
    string json = JsonWriter::ToJson(status, JsonWriter::PRETTY);
    RETURN_NOT_OK(WriteStringToFile(options_.env, Slice(json), path));
  } else if (boost::iequals(format, "pb")) {
    // TODO: Use PB container format?
    RETURN_NOT_OK(pb_util::WritePBToPath(options_.env, path, status,
                                         pb_util::NO_SYNC)); // durability doesn't matter
  } else {
    return Status::InvalidArgument("bad format", format);
  }

  LOG(INFO) << "Dumped server information to " << path;
  return Status::OK();
}

Status RpcServerBase::RegisterService(gscoped_ptr<rpc::ServiceIf> rpc_impl) {
  return rpc_server_->RegisterService(rpc_impl.Pass());
}

Status RpcServerBase::StartMetricsLogging() {
  if (options_.metrics_log_interval_ms <= 0) {
    return Status::OK();
  }

  return Thread::Create("server", "metrics-logger", &RpcAndWebServerBase::MetricsLoggingThread,
                        this, &metrics_logging_thread_);
}

void RpcServerBase::MetricsLoggingThread() {
  RollingLog log(Env::Default(), FLAGS_log_dir, "metrics");

  // How long to wait before trying again if we experience a failure
  // logging metrics.
  const MonoDelta kWaitBetweenFailures = MonoDelta::FromSeconds(60);


  MonoTime next_log = MonoTime::Now(MonoTime::FINE);
  while (!stop_metrics_logging_latch_.WaitUntil(next_log)) {
    next_log = MonoTime::Now(MonoTime::FINE);
    next_log.AddDelta(MonoDelta::FromMilliseconds(options_.metrics_log_interval_ms));

    std::stringstream buf;
    buf << "metrics " << GetCurrentTimeMicros() << " ";

    // Collect the metrics JSON string.
    vector<string> metrics;
    metrics.push_back("*");
    MetricJsonOptions opts;
    opts.include_raw_histograms = true;

    JsonWriter writer(&buf, JsonWriter::COMPACT);
    Status s = metric_registry_->WriteAsJson(&writer, metrics, opts);
    if (!s.ok()) {
      WARN_NOT_OK(s, "Unable to collect metrics to log");
      next_log.AddDelta(kWaitBetweenFailures);
      continue;
    }

    buf << "\n";

    s = log.Append(buf.str());
    if (!s.ok()) {
      WARN_NOT_OK(s, "Unable to write metrics to log");
      next_log.AddDelta(kWaitBetweenFailures);
      continue;
    }
  }

  WARN_NOT_OK(log.Close(), "Unable to close metric log");
}

Status RpcServerBase::Start() {
  RETURN_NOT_OK(RegisterService(make_gscoped_ptr<rpc::ServiceIf>(
      new GenericServiceImpl(this))));

  RETURN_NOT_OK(StartRpcServer());

  return Status::OK();
}

Status RpcServerBase::StartRpcServer() {
  CHECK(initialized_);

  RETURN_NOT_OK(rpc_server_->Start());

  if (!options_.dump_info_path.empty()) {
    RETURN_NOT_OK_PREPEND(DumpServerInfo(options_.dump_info_path, options_.dump_info_format),
                          "Failed to dump server info to " + options_.dump_info_path);
  }

  return Status::OK();
}

void RpcServerBase::Shutdown() {
  if (metrics_logging_thread_) {
    stop_metrics_logging_latch_.CountDown();
    metrics_logging_thread_->Join();
  }
  rpc_server_->Shutdown();
}

RpcAndWebServerBase::RpcAndWebServerBase(string name, const ServerBaseOptions& options,
                                         const string& metric_namespace)
    : RpcServerBase(name, options, metric_namespace),
      web_server_(new Webserver(options.webserver_opts, name_)) {
  FsManagerOpts fs_opts;
  fs_opts.metric_entity = metric_entity_;
  fs_opts.parent_mem_tracker = mem_tracker_;
  fs_opts.wal_path = options.fs_opts.wal_path;
  fs_opts.data_paths = options.fs_opts.data_paths;
  fs_manager_.reset(new FsManager(options.env, fs_opts));

  CHECK_OK(StartThreadInstrumentation(metric_entity_, web_server_.get()));
}

RpcAndWebServerBase::~RpcAndWebServerBase() {
  Shutdown();
}

Sockaddr RpcAndWebServerBase::first_http_address() const {
  vector<Sockaddr> addrs;
  WARN_NOT_OK(web_server_->GetBoundAddresses(&addrs),
              "Couldn't get bound webserver addresses");
  CHECK(!addrs.empty()) << "Not bound";
  return addrs[0];
}

void RpcAndWebServerBase::GenerateInstanceID() {
  instance_pb_.reset(new NodeInstancePB);
  instance_pb_->set_permanent_uuid(fs_manager_->uuid());
  // TODO: maybe actually bump a sequence number on local disk instead of
  // using time.
  instance_pb_->set_instance_seqno(Env::Default()->NowMicros());
}

Status RpcAndWebServerBase::Init() {
  RETURN_NOT_OK(RpcServerBase::Init());

  Status s = fs_manager_->Open();
  if (s.IsNotFound()) {
    LOG(INFO) << "Could not load existing FS layout: " << s.ToString();
    LOG(INFO) << "Creating new FS layout";
    is_first_run_ = true;
    RETURN_NOT_OK_PREPEND(fs_manager_->CreateInitialFileSystemLayout(),
                          "Could not create new FS layout");
    s = fs_manager_->Open();
  }
  RETURN_NOT_OK_PREPEND(s, "Failed to load FS layout");

  return Status::OK();
}

void RpcAndWebServerBase::GetStatusPB(ServerStatusPB* status) const {
  RpcServerBase::GetStatusPB(status);

  // HTTP ports
  {
    vector<Sockaddr> addrs;
    CHECK_OK(web_server_->GetBoundAddresses(&addrs));
    for (const Sockaddr& addr : addrs) {
      HostPortPB* pb = status->add_bound_http_addresses();
      pb->set_host(addr.host());
      pb->set_port(addr.port());
    }
  }
}

Status RpcAndWebServerBase::GetRegistration(ServerRegistrationPB* reg) const {
  vector<Sockaddr> addrs;
  RETURN_NOT_OK(CHECK_NOTNULL(rpc_server())->GetBoundAddresses(&addrs));
  RETURN_NOT_OK_PREPEND(
      AddHostPortPBs(addrs, reg->mutable_rpc_addresses()),
      "Failed to add RPC addresses to registration");

  addrs.clear();
  RETURN_NOT_OK_PREPEND(
      CHECK_NOTNULL(web_server())->GetBoundAddresses(&addrs), "Unable to get bound HTTP addresses");
  RETURN_NOT_OK_PREPEND(
      AddHostPortPBs(addrs, reg->mutable_http_addresses()),
      "Failed to add HTTP addresses to registration");

  reg->mutable_cloud_info()->set_placement_cloud(options_.placement_cloud);
  reg->mutable_cloud_info()->set_placement_region(options_.placement_region);
  reg->mutable_cloud_info()->set_placement_zone(options_.placement_zone);
  return Status::OK();
}

std::string RpcAndWebServerBase::FooterHtml() const {
  return Substitute("<pre>$0\nserver uuid $1</pre>",
                    VersionInfo::GetShortVersionString(),
                    instance_pb_->permanent_uuid());
}

Status RpcAndWebServerBase::Start() {
  GenerateInstanceID();

  AddDefaultPathHandlers(web_server_.get());
  AddRpczPathHandlers(messenger_, web_server_.get());
  RegisterMetricsJsonHandler(web_server_.get(), metric_registry_.get());
  TracingPathHandlers::RegisterHandlers(web_server_.get());
  web_server_->set_footer_html(FooterHtml());
  RETURN_NOT_OK(web_server_->Start());

  RETURN_NOT_OK(RpcServerBase::Start());

  return Status::OK();
}

void RpcAndWebServerBase::Shutdown() {
  RpcServerBase::Shutdown();
  web_server_->Stop();
}

} // namespace server
} // namespace yb
