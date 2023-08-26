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
//

#include <gflags/gflags_declare.h>
#include "yb/cdc/cdc_fwd.h"
#include "yb/cdc/cdc_service_context.h"
#include "yb/rpc/yb_rpc.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/status.h"
#include "yb/yql/cql/cqlserver/cql_server.h"

#include "yb/client/client.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_heartbeat.pb.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/connection_context.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/tserver/tablet_server_interface.h"

#include "yb/server/secure.h"
#include "yb/rpc/secure_stream.h"

#include "yb/util/flags.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/source_location.h"
#include "yb/master/master_service.h"
#include "yb/tserver/cdc_tserver_server.h"
#include "yb/tserver/cdc_tserver_service.h"
#include "yb/tserver/tablet_server.h"

DECLARE_string(cdc_certs);

namespace yb {
namespace cdcserver {

// TODO: This is implemented in tablet_server.cc. Resuse
class CDCServiceContextImpl : public cdc::CDCServiceContext {
 public:
  explicit CDCServiceContextImpl(tserver::TabletServer* tablet_server)
      : tablet_server_(*tablet_server) {}

  tablet::TabletPeerPtr LookupTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->LookupTablet(tablet_id);
  }

  Result<tablet::TabletPeerPtr> GetTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->GetTablet(tablet_id);
  }

  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->GetServingTablet(tablet_id);
  }

  const std::string& permanent_uuid() const override { return tablet_server_.permanent_uuid(); }

  std::unique_ptr<client::AsyncClientInitialiser> MakeClientInitializer(
      const std::string& client_name, MonoDelta default_timeout) const override {
    return std::make_unique<client::AsyncClientInitialiser>(
        client_name, default_timeout, tablet_server_.permanent_uuid(), &tablet_server_.options(),
        tablet_server_.metric_entity(), tablet_server_.mem_tracker(), tablet_server_.messenger());
  }

 private:
  tserver::TabletServer& tablet_server_;
};

// TODO: Verify params passed to to RpcServerBase (metrics, mem_tracker)
CDCTServerServer::CDCTServerServer(tserver::TabletServer* tserver, const CDCServerOptions& opts)
    : RpcAndWebServerBase(
          "CDCTServerServer", opts, "yb.cdctserverserver",
          MemTracker::CreateTracker(
              "CDC_TServer", MemTracker::GetRootTracker(), AddToParent::kTrue,
              CreateMetrics::kFalse)),
      opts_(opts),
      tserver_(tserver) {

  // TODO : Verify if this connection context is appropriate
  SetConnectionContextFactory(
      rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(0, mem_tracker()));
}

Status CDCTServerServer::Start() {
  RETURN_NOT_OK(server::RpcAndWebServerBase::Init());
  RETURN_NOT_OK(RegisterServices());
  RETURN_NOT_OK(server::RpcAndWebServerBase::Start());

  return Status::OK();
}

Status CDCTServerServer::RegisterServices() {
  size_t queue_limit = 1000;
  RETURN_NOT_OK(RegisterService(
      queue_limit, // queue_limit
      std::make_unique<CDCTServerServiceImpl>(
          std::make_unique<CDCServiceContextImpl>(tserver_), metric_entity(), metric_registry())));

  return Status::OK();
}

void CDCTServerServer::Shutdown() { server::RpcAndWebServerBase::Shutdown(); }

Status CDCTServerServer::ReloadKeysAndCertificates() {
  if (!secure_context_) {
    return Status::OK();
  }

  return server::ReloadSecureContextKeysAndCertificates(
      secure_context_.get(),
      fs_manager_->GetDefaultRootDir(),
      server::SecureContextType::kCDC,
      options_.HostsString());
}

Status CDCTServerServer::SetupMessengerBuilder(rpc::MessengerBuilder* builder) {
  LOG(INFO) <<"Sid: Called CDCTServerServer::SetupMessengerBuilder";
  RETURN_NOT_OK(server::RpcAndWebServerBase::SetupMessengerBuilder(builder));
  if(!FLAGS_cert_node_filename.empty()) {
      secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
        fs_manager_->GetDefaultRootDir(),
        FLAGS_cert_node_filename,
        server::SecureContextType::kCDC,
        builder));
  } else {
    secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
        options_.HostsString(), *fs_manager_, server::SecureContextType::kCDC, builder));
  }
  return Status::OK();
}

}  // namespace cdcserver
}  // namespace yb
