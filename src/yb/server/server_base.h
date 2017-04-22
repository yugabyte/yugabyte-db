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
#ifndef YB_SERVER_SERVER_BASE_H
#define YB_SERVER_SERVER_BASE_H

#include <memory>
#include <string>

#include "yb/common/wire_protocol.pb.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/rpc/connection_types.h"
#include "yb/rpc/service_if.h"
#include "yb/server/server_base_options.h"
#include "yb/util/status.h"

namespace yb {

class Env;
class FsManager;
class MemTracker;
class MetricEntity;
class MetricRegistry;
class NodeInstancePB;
class RpcServer;
class ScopedGLogMetrics;
class Sockaddr;
class Thread;
class Webserver;

namespace rpc {
class Messenger;
class ServiceIf;
class ServicePoolOpts;
} // namespace rpc

namespace server {
class Clock;

class ServerBaseOptions;
class ServerStatusPB;

// Base class that is common to implementing a Redis server, as well as
// a YB tablet server and master.
class RpcServerBase {
 public:
  const RpcServer *rpc_server() const { return rpc_server_.get(); }
  const std::shared_ptr<rpc::Messenger>& messenger() const { return messenger_; }

  // Return the first RPC address that this server has bound to.
  // FATALs if the server is not started.
  Sockaddr first_rpc_address() const;

  // Return the instance identifier of this server.
  // This may not be called until after the server is Initted.
  const NodeInstancePB& instance_pb() const;

  const std::shared_ptr<MemTracker>& mem_tracker() const { return mem_tracker_; }

  const scoped_refptr<MetricEntity>& metric_entity() const { return metric_entity_; }

  MetricRegistry* metric_registry() { return metric_registry_.get(); }

  // Returns this server's clock.
  Clock* clock() { return clock_.get(); }

  virtual std::string ToString() const;

  // Return a PB describing the status of the server (version info, bound ports, etc)
  virtual void GetStatusPB(ServerStatusPB* status) const;

 protected:
  RpcServerBase(std::string name,
                const ServerBaseOptions& options,
                const std::string& metrics_namespace);
  virtual ~RpcServerBase();

  CHECKED_STATUS Init();
  CHECKED_STATUS RegisterService(size_t queue_limit, gscoped_ptr<rpc::ServiceIf> rpc_impl);
  CHECKED_STATUS Start();
  CHECKED_STATUS StartRpcServer();
  void Shutdown();

  const std::string name_;

  std::shared_ptr<MemTracker> mem_tracker_;
  gscoped_ptr<MetricRegistry> metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
  gscoped_ptr<RpcServer> rpc_server_;
  std::shared_ptr<rpc::Messenger> messenger_;
  bool is_first_run_;

  const rpc::ConnectionType connection_type_;

  scoped_refptr<Clock> clock_;

  // The instance identifier of this server.
  gscoped_ptr<NodeInstancePB> instance_pb_;

  ServerBaseOptions options_;

  virtual CHECKED_STATUS DumpServerInfo(const std::string& path,
                        const std::string& format) const;

  bool initialized_;
 private:
  CHECKED_STATUS StartMetricsLogging();
  void MetricsLoggingThread();

  scoped_refptr<Thread> metrics_logging_thread_;
  CountDownLatch stop_metrics_logging_latch_;

  gscoped_ptr<ScopedGLogMetrics> glog_metrics_;

  DISALLOW_COPY_AND_ASSIGN(RpcServerBase);
};

// Base class for tablet server and master.
// Handles starting and stopping the RPC server and web server,
// and provides a common interface for server-type-agnostic functions.
class RpcAndWebServerBase : public RpcServerBase {
 public:
  const Webserver *web_server() const { return web_server_.get(); }

  FsManager* fs_manager() { return fs_manager_.get(); }

  // Return the first HTTP address that this server has bound to.
  // FATALs if the server is not started.
  Sockaddr first_http_address() const;

  // Return a PB describing the status of the server (version info, bound ports, etc)
  void GetStatusPB(ServerStatusPB* status) const override;

  // Centralized method to get the Registration information for either the Master or Tserver.
  CHECKED_STATUS GetRegistration(ServerRegistrationPB* reg) const;

 protected:
  RpcAndWebServerBase(std::string name, const ServerBaseOptions& options,
             const std::string& metrics_namespace);
  virtual ~RpcAndWebServerBase();

  CHECKED_STATUS Init();
  CHECKED_STATUS Start();
  void Shutdown();

  gscoped_ptr<FsManager> fs_manager_;
  gscoped_ptr<Webserver> web_server_;

 private:
  void GenerateInstanceID();
  std::string FooterHtml() const;

  DISALLOW_COPY_AND_ASSIGN(RpcAndWebServerBase);
};

} // namespace server
} // namespace yb
#endif /* YB_SERVER_SERVER_BASE_H */
