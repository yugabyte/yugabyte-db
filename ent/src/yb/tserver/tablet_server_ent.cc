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

#include "yb/cdc/cdc_service.h"

#include "yb/rpc/secure_stream.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/secure.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/backup_service.h"
#include "yb/tserver/header_manager_impl.h"
#include "yb/tserver/cdc_consumer.h"

#include "yb/util/flags.h"
#include "yb/util/flag_tags.h"
#include "yb/util/ntp_clock.h"
#include "yb/util/encrypted_file_factory.h"
#include "yb/util/universe_key_manager.h"

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

DEFINE_int32(ts_backup_svc_num_threads, 4,
             "Number of RPC worker threads for the TS backup service");
TAG_FLAG(ts_backup_svc_num_threads, advanced);

DEFINE_int32(ts_backup_svc_queue_length, 50,
             "RPC queue length for the TS backup service");
TAG_FLAG(ts_backup_svc_queue_length, advanced);

DECLARE_int32(svc_queue_length_default);

namespace yb {
namespace tserver {
namespace enterprise {

using cdc::CDCServiceImpl;
using yb::rpc::ServiceIf;

TabletServer::TabletServer(const TabletServerOptions& opts) :
  super(opts),
  universe_key_manager_(std::make_unique<yb::enterprise::UniverseKeyManager>()),
  env_(yb::enterprise::NewEncryptedEnv(DefaultHeaderManager(universe_key_manager_.get()))),
  rocksdb_env_(yb::enterprise::NewRocksDBEncryptedEnv(
      DefaultHeaderManager(universe_key_manager_.get()))) {}

TabletServer::~TabletServer() {
}

Status TabletServer::RegisterServices() {
#if !defined(__APPLE__)
  server::HybridClock::RegisterProvider(NtpClock::Name(), [] {
    return std::make_shared<NtpClock>();
  });
#endif

  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(
      FLAGS_ts_backup_svc_queue_length,
      std::make_unique<TabletServiceBackupImpl>(tablet_manager_.get(), metric_entity())));

  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(
      FLAGS_svc_queue_length_default,
      std::make_unique<CDCServiceImpl>(tablet_manager_.get(), metric_entity())));

  return super::RegisterServices();
}

Status TabletServer::SetupMessengerBuilder(rpc::MessengerBuilder* builder) {
  RETURN_NOT_OK(super::SetupMessengerBuilder(builder));
  secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
      options_.rpc_opts.rpc_bind_addresses, fs_manager_.get(),
      server::SecureContextType::kServerToServer, builder));
  return Status::OK();
}

Env* TabletServer::GetEnv() {
  return env_.get();
}

rocksdb::Env* TabletServer::GetRocksDBEnv() {
  return rocksdb_env_.get();
}

CDCConsumer* TabletServer::GetCDCConsumer() {
  std::unique_lock<std::mutex> l(cdc_consumer_mutex_);
  return cdc_consumer_.get();
}

yb::enterprise::UniverseKeyManager* TabletServer::GetUniverseKeyManager() {
  return universe_key_manager_.get();
}

Status TabletServer::SetUniverseKeyRegistry(
    const yb::UniverseKeyRegistryPB& universe_key_registry) {
  universe_key_manager_->SetUniverseKeyRegistry(universe_key_registry);
  return Status::OK();
}

Status TabletServer::CreateCDCConsumer() {
  std::unique_lock<std::mutex> l(cdc_consumer_mutex_);
  auto is_leader_clbk = [this](const string& tablet_id){
    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    if (!tablet_manager_->LookupTablet(tablet_id, &tablet_peer)) {
      return false;
    }
    return tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
  };
  cdc_consumer_ = VERIFY_RESULT(CDCConsumer::Create(std::move(is_leader_clbk), proxy_cache_.get(),
                                                    permanent_uuid()));
  return Status::OK();
}

Status TabletServer::SetConsumerRegistry(const cdc::ConsumerRegistryPB& consumer_registry) {
  if (!cdc_consumer_.get()) {
    RETURN_NOT_OK(CreateCDCConsumer());
  }
  cdc_consumer_->RefreshWithNewRegistryFromMaster(consumer_registry);
  return Status::OK();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
