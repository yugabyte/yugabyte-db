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

// The C++ process host of the Rust master: the server base, the sys-catalog tablet and the
// replicated state machines that run inside its Raft apply (snapshot coordinator, AutoFlags), but
// no CatalogManager. Rust drives it through master/master/src/ffi.rs and serves the master RPCs.
//
// In layer 1 the host is a follower only: it never starts an election and steps down at once if it
// wins one, because nothing in Rust can act as a leader yet.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include <boost/asio/ip/tcp.hpp>

#include "rust/cxx.h"

#include "yb/consensus/consensus.pb.h"

#include "yb/docdb/docdb_compaction_context.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_options.h"

#include "yb/rust_bridge/rpc_bridge.h"

#include "yb/server/webserver_options.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/db_server_base.h"

#include "yb/common/termination_monitor.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/net/net_util.h"

namespace yb {

class MaintenanceManager;
class ServerEntryPB;
class TerminationMonitor;
class Thread;
class ThreadPool;

namespace server {
class TotalMemWatcher;
}  // namespace server

namespace master {
class EncryptionManager;
class MasterAutoFlagsManager;
class MasterSnapshotCoordinator;
class SysCatalogTable;
}  // namespace master

namespace master_host {

class MasterInfoHandle;
// Defined in Rust (master/master/src/ffi.rs).
struct WebMasterList;

class MasterHost : public tserver::DbServerBase, public rust_bridge::RpcServerHandle {
 public:
  // `public_web_opts` are the web server settings from the flags, which the Rust web server serves;
  // `opts.webserver_opts` are those of the C++ Webserver behind it.
  MasterHost(const master::MasterOptions& opts, WebserverOptions public_web_opts);
  ~MasterHost();

  // Bridge API (master/master/src/ffi.rs). Failures throw std::runtime_error, which cxx turns into
  // a Rust Err.

  // Creates the file system layout, messenger and web server, and loads the sys catalog's
  // replicated state machines' persistent state.
  void InitHost();
  rust_bridge::RpcServerHandle& ServerHandle() { return *this; }
  // Registers the engine services (consensus, remote bootstrap), binds RPC, and opens the sys
  // catalog tablet, or enters shell mode when there is none. Rust registers its services before.
  void StartHost();
  // Blocks until the process is asked to terminate (SIGTERM, SIGINT).
  void WaitForTermination() const;
  void ShutdownHost();
  // Access for Rust handlers, which run on other threads while the main thread owns the host.
  // Must not outlive it.
  std::unique_ptr<MasterInfoHandle> InfoHandle();

  rust::Vec<uint8_t> InstancePB() const;
  rust::Vec<uint8_t> RegistrationPB() const;
  rust::Vec<uint8_t> CommittedConsensusStatePB() const;
  int32_t RaftRole() const;
  bool IsSysCatalogInitialized() const;
  bool IsInShellMode() const;
  rust::Vec<uint8_t> FlushSysCatalogResponse() const;
  rust::Vec<uint8_t> CompactSysCatalogResponse() const;
  // The master RPCs every C++ master serves from state the host has (RemovedMasterUpdate,
  // CheckMasterTabletHealth, the universe key RPCs, DumpSysCatalogEntries,
  // WriteSysCatalogEntry): the serialized response to a serialized request. The caller checks
  // initialization where C++ does.
  rust::Vec<uint8_t> HandleOnHost(rust::Str method, rust::Slice<const uint8_t> request);
  int32_t MasterServiceQueueLength() const;
  int32_t YsqlLeaseServiceQueueLength() const;

  // Web. Rust serves the web port and proxies it to the C++ Webserver on an ephemeral loopback
  // port, where the host serves the pages a C++ follower master serves.

  rust::String PublicWebInterface() const;
  uint16_t PublicWebPort() const;
  rust::String WebCertificateFile() const;
  rust::String WebPrivateKeyFile() const;
  rust::Vec<uint8_t> WebPrivateKeyPassword() const;
  rust::String WebSslCiphers() const;
  // The addresses the Rust web server bound, and the source of the master list the web pages
  // show. Must be called before StartHost(), which advertises the addresses.
  void SetWebServer(
      const rust::Vec<rust::String>& bound_ips, uint16_t bound_port,
      rust::Box<WebMasterList> master_list);
  uint16_t LoopbackWebPort() const;

  // RpcServerHandle.
  Status RegisterRustService(
      size_t queue_limit, rpc::ServiceIfPtr service, rpc::ServicePriority priority) override;
  rpc::ProxyCache& GetProxyCache() override { return proxy_cache(); }
  const scoped_refptr<MetricEntity>& GetMetricEntity() const override { return metric_entity(); }

  // DbServerBase.
  Status InitFlags(rpc::Messenger* messenger) override;
  Status GetRegistration(
      ServerRegistrationPB* reg, server::RpcOnly rpc_only = server::RpcOnly::kFalse) const override;
  void GetStatusPB(server::ServerStatusPB* status) const override;
  MonoDelta default_client_timeout() override;
  void SetupAsyncClientInit(client::AsyncClientInitializer* async_client_init) override;
  client::LocalTabletFilter CreateLocalTabletFilter() override;
  void WriteServerMetaCacheAsJson(JsonWriter* writer) override;
  const std::string& permanent_uuid() const override;
  void RegisterCertificateReloader(tserver::CertificateReloader reloader) override {}
  void RegisterPgProcessRestarter(std::function<Status(void)> restarter) override {}
  void RegisterPgProcessKiller(std::function<Status(void)> killer) override {}
  void RegisterPgConfigGenerator(pgwrapper::PgConfigGenerator generator) override {}
  uint32_t GetAutoFlagConfigVersion() const override;
  Result<std::unordered_set<std::string>> GetAvailableAutoFlagsForServer() const override;
  Result<std::unordered_set<std::string>> GetFlagsForServer() const override;
  Status ReloadKeysAndCertificates() override;
  std::string GetCertificateDetails() override;

 protected:
  Status SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

 private:
  class SysCatalogContextImpl;
  class SnapshotContextImpl;
  class TabletPeerLookupImpl;

  Status DoInit();
  Status DoStart();
  Status OpenSysCatalog();
  Status ElectedAsLeader();
  Status ResetMemoryState(const consensus::RaftConfigPB& config);
  Status GetMasterRegistration(ServerRegistrationPB* reg) const;
  Status GoIntoShellMode();
  void RegisterWebPages();
  Result<std::vector<ServerEntryPB>> ListMastersForWeb() const;
  docdb::HistoryCutoff AllowedHistoryCutoff(tablet::RaftGroupMetadata* metadata);
  void RefreshHistoryRetentionPinLoop();
  Status StartRemoteBootstrap(const consensus::StartRemoteBootstrapRequestPB& req);

  master::MasterOptions opts_;
  std::unique_ptr<SysCatalogContextImpl> sys_catalog_context_;
  std::unique_ptr<SnapshotContextImpl> snapshot_context_;
  std::unique_ptr<TabletPeerLookupImpl> tablet_peer_lookup_;
  std::unique_ptr<master::SysCatalogTable> sys_catalog_;
  std::unique_ptr<master::MasterSnapshotCoordinator> snapshot_coordinator_;
  std::unique_ptr<master::MasterAutoFlagsManager> auto_flags_manager_;
  // Holds the universe keys AddUniverseKeys sends to every master.
  std::unique_ptr<master::EncryptionManager> encryption_manager_;
  std::shared_ptr<MaintenanceManager> maintenance_manager_;
  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<ThreadPool> leadership_pool_;

  // Published by the C++ master leader; followers hold their pg catalog history back to it.
  master::HistoryRetentionPinInfo ysql_history_retention_pin_;
  scoped_refptr<Thread> pin_refresh_thread_;
  std::unique_ptr<server::TotalMemWatcher> total_mem_watcher_;
  scoped_refptr<Thread> total_mem_watcher_thread_;
  CountDownLatch stop_latch_{1};
  std::unique_ptr<TerminationMonitor> termination_monitor_;

  AtomicUniquePtr<ServerRegistrationPB> registration_;
  const WebserverOptions public_web_opts_;
  std::vector<Endpoint> public_web_endpoints_;
  std::optional<rust::Box<WebMasterList>> web_master_list_;
  std::atomic<bool> sys_catalog_initialized_{false};
  bool sys_catalog_started_ = false;
  // Serializes remote bootstrap requests, as CatalogManager does.
  std::mutex remote_bootstrap_mutex_;
  bool tablet_exists_ = false;
};

class MasterInfoHandle {
 public:
  explicit MasterInfoHandle(MasterHost* host) : host_(*host) {}

  rust::Vec<uint8_t> InstancePB() const { return host_.InstancePB(); }
  rust::Vec<uint8_t> RegistrationPB() const { return host_.RegistrationPB(); }
  rust::Vec<uint8_t> CommittedConsensusStatePB() const {
    return host_.CommittedConsensusStatePB();
  }
  int32_t RaftRole() const { return host_.RaftRole(); }
  bool IsSysCatalogInitialized() const { return host_.IsSysCatalogInitialized(); }
  bool IsInShellMode() const { return host_.IsInShellMode(); }
  rust::Vec<uint8_t> FlushSysCatalogResponse() const { return host_.FlushSysCatalogResponse(); }
  rust::Vec<uint8_t> CompactSysCatalogResponse() const {
    return host_.CompactSysCatalogResponse();
  }
  rust::Vec<uint8_t> HandleOnHost(rust::Str method, rust::Slice<const uint8_t> request) const {
    return host_.HandleOnHost(method, request);
  }

 private:
  MasterHost& host_;
};

std::unique_ptr<MasterHost> CreateMasterHost();

}  // namespace master_host
}  // namespace yb
