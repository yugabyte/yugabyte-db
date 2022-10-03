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

#include "yb/tserver/tablet_server.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service_context.h"

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/rpc/secure_stream.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/secure.h"

#include "yb/rpc/rpc.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/backup_service.h"
#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/flags.h"
#include "yb/util/flag_tags.h"
#include "yb/util/ntp_clock.h"

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

DEFINE_int32(ts_backup_svc_num_threads, 4,
             "Number of RPC worker threads for the TS backup service");
TAG_FLAG(ts_backup_svc_num_threads, advanced);

DEFINE_int32(ts_backup_svc_queue_length, 50,
             "RPC queue length for the TS backup service");
TAG_FLAG(ts_backup_svc_queue_length, advanced);

DEFINE_int32(xcluster_svc_queue_length, 5000,
             "RPC queue length for the xCluster service");
TAG_FLAG(xcluster_svc_queue_length, advanced);

DECLARE_int32(svc_queue_length_default);

DECLARE_string(cert_node_filename);

namespace yb {
namespace tserver {
namespace enterprise {

namespace {

class CDCServiceContextImpl : public cdc::CDCServiceContext {
 public:
  explicit CDCServiceContextImpl(TabletServer* tablet_server)
      : tablet_server_(*tablet_server) {
  }

  tablet::TabletPeerPtr LookupTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->LookupTablet(tablet_id);
  }

  Result<tablet::TabletPeerPtr> GetTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->GetTablet(tablet_id);
  }

  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->GetServingTablet(tablet_id);
  }

  const std::string& permanent_uuid() const override {
    return tablet_server_.permanent_uuid();
  }

  std::unique_ptr<client::AsyncClientInitialiser> MakeClientInitializer(
      const std::string& client_name, MonoDelta default_timeout) const override {
    return std::make_unique<client::AsyncClientInitialiser>(
        client_name, default_timeout, tablet_server_.permanent_uuid(), &tablet_server_.options(),
        tablet_server_.metric_entity(), tablet_server_.mem_tracker(), tablet_server_.messenger());
  }

 private:
  TabletServer& tablet_server_;
};

} // namespace

TabletServer::TabletServer(const TabletServerOptions& opts)
  : super(opts) {}

TabletServer::~TabletServer() {
  Shutdown();
}

void TabletServer::Shutdown() {
  auto cdc_consumer = GetCDCConsumer();
  if (cdc_consumer) {
    cdc_consumer->Shutdown();
  }
  super::Shutdown();
}

Status TabletServer::RegisterServices() {
#if !defined(__APPLE__)
  server::HybridClock::RegisterProvider(NtpClock::Name(), [](const std::string&) {
    return std::make_shared<NtpClock>();
  });
#endif

  cdc_service_ = std::make_shared<cdc::CDCServiceImpl>(
      std::make_unique<CDCServiceContextImpl>(this), metric_entity(),
      metric_registry());

  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(
      FLAGS_ts_backup_svc_queue_length,
      std::make_unique<TabletServiceBackupImpl>(tablet_manager_.get(), metric_entity())));

  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(
      FLAGS_xcluster_svc_queue_length,
      cdc_service_));

  return super::RegisterServices();
}

Status TabletServer::SetupMessengerBuilder(rpc::MessengerBuilder* builder) {
  RETURN_NOT_OK(super::SetupMessengerBuilder(builder));

  secure_context_ = VERIFY_RESULT(
      server::SetupInternalSecureContext(options_.HostsString(), *fs_manager_, builder));

  return Status::OK();
}

CDCConsumer* TabletServer::GetCDCConsumer() {
  std::lock_guard<decltype(cdc_consumer_mutex_)> l(cdc_consumer_mutex_);
  return cdc_consumer_.get();
}

encryption::UniverseKeyManager* TabletServer::GetUniverseKeyManager() {
  return opts_.universe_key_manager;
}

Status TabletServer::SetUniverseKeyRegistry(
    const encryption::UniverseKeyRegistryPB& universe_key_registry) {
  opts_.universe_key_manager->SetUniverseKeyRegistry(universe_key_registry);
  return Status::OK();
}

Status TabletServer::CreateCDCConsumer() {
  auto is_leader_clbk = [this](const string& tablet_id){
    auto tablet_peer = tablet_manager_->LookupTablet(tablet_id);
    if (!tablet_peer) {
      return false;
    }
    return tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
  };

  cdc_consumer_ = VERIFY_RESULT(
    CDCConsumer::Create(std::move(is_leader_clbk), proxy_cache_.get(), this));
  return Status::OK();
}

Status TabletServer::SetConfigVersionAndConsumerRegistry(int32_t cluster_config_version,
    const cdc::ConsumerRegistryPB* consumer_registry) {
  std::lock_guard<decltype(cdc_consumer_mutex_)> l(cdc_consumer_mutex_);

  // Only create a cdc consumer if consumer_registry is not null.
  if (!cdc_consumer_ && consumer_registry) {
    RETURN_NOT_OK(CreateCDCConsumer());
  }
  if (cdc_consumer_) {
    cdc_consumer_->RefreshWithNewRegistryFromMaster(consumer_registry, cluster_config_version);
  }
  return Status::OK();
}

int32_t TabletServer::cluster_config_version() const {
  std::lock_guard<decltype(cdc_consumer_mutex_)> l(cdc_consumer_mutex_);
  // If no CDC consumer, we will return -1, which will force the master to send the consumer
  // registry if one exists. If we receive one, we will create a new CDC consumer in
  // SetConsumerRegistry.
  if (!cdc_consumer_) {
    return -1;
  }
  return cdc_consumer_->cluster_config_version();
}

Status TabletServer::ReloadKeysAndCertificates() {
  if (!secure_context_) {
    return Status::OK();
  }

  RETURN_NOT_OK(server::ReloadSecureContextKeysAndCertificates(
        secure_context_.get(),
        fs_manager_->GetDefaultRootDir(),
        server::SecureContextType::kInternal,
        options_.HostsString()));

  std::lock_guard<decltype(cdc_consumer_mutex_)> l(cdc_consumer_mutex_);
  if (cdc_consumer_) {
    RETURN_NOT_OK(cdc_consumer_->ReloadCertificates());
  }

  for (const auto& reloader : certificate_reloaders_) {
    RETURN_NOT_OK(reloader());
  }

  return Status::OK();
}

std::string TabletServer::GetCertificateDetails() {
  if(!secure_context_) return "";

  return secure_context_.get()->GetCertificateDetails();
}

void TabletServer::RegisterCertificateReloader(CertificateReloader reloader) {
  certificate_reloaders_.push_back(std::move(reloader));
}

Status TabletServer::SetCDCServiceEnabled() {
  if (!cdc_service_) {
    LOG(WARNING) << "CDC Service Not Registered";
  } else {
    cdc_service_->SetCDCServiceEnabled();
  }
  return Status::OK();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
