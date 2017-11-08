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
#include "yb/server/server_base.h"

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <gflags/gflags.h>

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

DEFINE_int32(num_reactor_threads, -1,
             "Number of libev reactor threads to start. If -1, the value is automatically set.");
TAG_FLAG(num_reactor_threads, advanced);

DECLARE_bool(use_hybrid_clock);

DEFINE_int32(generic_svc_num_threads, 10,
             "Number of RPC worker threads to run for the generic service");
TAG_FLAG(generic_svc_num_threads, advanced);

DEFINE_int32(generic_svc_queue_length, 50,
             "RPC Queue length for the generic service");
TAG_FLAG(generic_svc_queue_length, advanced);

DEFINE_string(yb_test_name, "",
              "Specifies test name this daemon is running as part of.");

using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using strings::Substitute;

namespace yb {
namespace server {

static const string kWildCardHostAddress = "0.0.0.0";

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
      rpc_server_(new RpcServer(name_, options.rpc_opts)),
      is_first_run_(false),
      connection_context_factory_(options.connection_context_factory),
      options_(options),
      initialized_(false),
      stop_metrics_logging_latch_(1) {
  if (FLAGS_use_hybrid_clock) {
    clock_ = new HybridClock();
  } else {
    clock_ = LogicalClock::CreateStartingAt(HybridTime::kInitialHybridTime);
  }
}

RpcServerBase::~RpcServerBase() {
  Shutdown();
  mem_tracker_->UnregisterFromParent();
}

Endpoint RpcServerBase::first_rpc_address() const {
  const auto& addrs = rpc_server_->GetBoundAddresses();
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

  SetStackTraceSignal(SIGUSR2);

  // Initialize the clock immediately. This checks that the clock is synchronized
  // so we're less likely to get into a partially initialized state on disk during startup
  // if we're having clock problems.
  RETURN_NOT_OK_PREPEND(clock_->Init(), "Cannot initialize clock");

  // Create the Messenger.
  rpc::MessengerBuilder builder(name_);

  if (FLAGS_num_reactor_threads == -1) {
    // Auto set the number of reactors based on the number of cores.
    // But bound it between 2 & 8.
    LOG(INFO) << "Auto setting FLAGS_num_reactor_threads...";
    const int32 num_cores = std::thread::hardware_concurrency();
    const int32 num_reactors = std::min(8, num_cores);
    FLAGS_num_reactor_threads = std::max(2, num_reactors);
  }
  LOG(INFO) << "FLAGS_num_reactor_threads=" << FLAGS_num_reactor_threads;

  builder.set_num_reactors(FLAGS_num_reactor_threads);
  builder.set_metric_entity(metric_entity());
  builder.use_connection_context_factory(connection_context_factory_);
  RETURN_NOT_OK(builder.Build(&messenger_));

  RETURN_NOT_OK(rpc_server_->Init(messenger_));
  RETURN_NOT_OK(rpc_server_->Bind());
  clock_->RegisterMetrics(metric_entity_);

  RETURN_NOT_OK_PREPEND(StartMetricsLogging(), "Could not enable metrics logging");

  initialized_ = true;
  return Status::OK();
}

std::string RpcServerBase::ToString() const {
  return strings::Substitute("$0 : rpc=$1", name_, yb::ToString(first_rpc_address()));
}

void RpcServerBase::GetStatusPB(ServerStatusPB* status) const {
  // Node instance
  status->mutable_node_instance()->CopyFrom(*instance_pb_);

  // RPC ports
  {
    for (const auto& addr : rpc_server_->GetBoundAddresses()) {
      HostPortPB* pb = status->add_bound_rpc_addresses();
      pb->set_host(addr.address().to_string());
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
    return STATUS(InvalidArgument, "bad format", format);
  }

  LOG(INFO) << "Dumped server information to " << path;
  return Status::OK();
}

Status RpcServerBase::RegisterService(size_t queue_limit,
                                      std::unique_ptr<rpc::ServiceIf> rpc_impl) {
  return rpc_server_->RegisterService(queue_limit, std::move(rpc_impl));
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
  std::unique_ptr<rpc::ServiceIf> gsvc_impl(new GenericServiceImpl(this));
  RETURN_NOT_OK(RegisterService(FLAGS_generic_svc_queue_length, std::move(gsvc_impl)));

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
  fs_opts.wal_paths = options.fs_opts.wal_paths;
  fs_opts.data_paths = options.fs_opts.data_paths;
  fs_opts.server_type = options.server_type;
  fs_manager_.reset(new FsManager(options.env, fs_opts));

  CHECK_OK(StartThreadInstrumentation(metric_entity_, web_server_.get()));
}

RpcAndWebServerBase::~RpcAndWebServerBase() {
  Shutdown();
}

Endpoint RpcAndWebServerBase::first_http_address() const {
  std::vector<Endpoint> addrs;
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
    std::vector<Endpoint> addrs;
    CHECK_OK(web_server_->GetBoundAddresses(&addrs));
    for (const auto& addr : addrs) {
      HostPortPB* pb = status->add_bound_http_addresses();
      pb->set_host(addr.address().to_string());
      pb->set_port(addr.port());
    }
  }
}

Status RpcAndWebServerBase::GetRegistration(ServerRegistrationPB* reg) const {
  std::vector<HostPort> addrs = CHECK_NOTNULL(rpc_server())->GetRpcHostPort();

  // Fall back to hostname resolution if the rpc hostname is a wildcard.
  if (addrs.size() > 1 || addrs.at(0).host() == kWildCardHostAddress || addrs.at(0).port() == 0) {
    auto addrs = CHECK_NOTNULL(rpc_server())->GetBoundAddresses();
    RETURN_NOT_OK_PREPEND(AddHostPortPBs(addrs, reg->mutable_rpc_addresses()),
                          "Failed to add RPC endpoints to registration");
  } else {
    RETURN_NOT_OK_PREPEND(HostPortsToPBs(addrs, reg->mutable_rpc_addresses()),
                          "Failed to add RPC addresses to registration");
    LOG(INFO) << "Using private ip address " << reg->rpc_addresses(0).host();
  }

  std::vector<Endpoint> web_addrs;
  RETURN_NOT_OK_PREPEND(CHECK_NOTNULL(web_server())->GetBoundAddresses(&web_addrs),
                        "Unable to get bound HTTP addresses");
  RETURN_NOT_OK_PREPEND(AddHostPortPBs(web_addrs, reg->mutable_http_addresses()),
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

static void DisplayIconTile(std::stringstream* output, const string icon,
                            const string caption, const string url) {
  *output << "  <div class='col-sm-6 col-md-4 dbg-tile'>\n"
          << "    <a href='" << url << "' class='thumbnail'>\n"
          << "      <div class='dbg-icon'>\n"
          << "        <i class='fa " << icon << "' aria-hidden='true'></i>\n"
          << "      </div>\n"
          << "      <div class='caption dbg-caption'>\n"
          << "        <h3>" << caption << "</h3>\n"
          << "      </div> <!-- caption -->\n"
          << "    </a> <!-- thumbnail -->\n"
          << "  </div> <!-- col-xs-6 col-md-3 -->\n";
}

static void HandleDebugPage(const Webserver::WebRequest& req, stringstream* output) {
  *output << "<h1>Debug Utilities</h1>\n";

  *output << "<div class='row debug-tiles'>\n";

  // Logs.
  DisplayIconTile(output, "fa-files-o", "Logs", "/logs");
  // GFlags.
  DisplayIconTile(output, "fa-flag-o", "GFlags", "/varz");
  // Memory trackers.
  DisplayIconTile(output, "fa-bar-chart", "Memory Breakdown", "/mem-trackers");
  // Metrics.
  DisplayIconTile(output, "fa-line-chart", "Metrics", "/metrics");
  // RPCs in progress.
  DisplayIconTile(output, "fa-tasks", "RPCs In Progress", "/rpcz");
  // Threads.
  DisplayIconTile(output, "fa-list-ul", "Threads", "/threadz");
  // Total memory.
  DisplayIconTile(output, "fa-cog", "Total Memory", "/memz");

  *output << "</div> <!-- row -->\n";
}

Status RpcAndWebServerBase::Start() {
  GenerateInstanceID();

  AddDefaultPathHandlers(web_server_.get());
  AddRpczPathHandlers(messenger_, web_server_.get());
  RegisterMetricsJsonHandler(web_server_.get(), metric_registry_.get());
  TracingPathHandlers::RegisterHandlers(web_server_.get());
  web_server_->RegisterPathHandler("/utilz", "Utilities", HandleDebugPage,
                                   true, true, "fa fa-wrench");
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
