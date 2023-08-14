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
#include "yb/rpc/yb_rpc.h"
#include "yb/util/status.h"
#include "yb/yql/cql/cqlserver/cql_server.h"

#include <boost/bind.hpp>

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
#include "yb/master/cdc_master_server.h"

namespace yb {
namespace cdcserver {

using namespace yb::size_literals;

// TODO: Verify params passed to to RpcServerBase (metrics, mem_tracker)
CDCMasterServer::CDCMasterServer(master::Master* master, const CDCServerOptions& opts)
    : RpcServerBase(
          "CDCMasterServer", opts, "yb.cdcmasterserver",
          MemTracker::CreateTracker(
              "CDC_Master", MemTracker::GetRootTracker(), AddToParent::kTrue,
              CreateMetrics::kFalse)),
      opts_(opts),
      master_(master) {
  // TODO : Verify if this connection context is appropriate
  SetConnectionContextFactory(
      rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(0, mem_tracker()));
}

Status CDCMasterServer::Start() {
  RETURN_NOT_OK(server::RpcServerBase::Init());
  RETURN_NOT_OK(RegisterServices());
  RETURN_NOT_OK(server::RpcServerBase::Start());

  return Status::OK();
}

Status CDCMasterServer::RegisterServices() {
  size_t queue_limit = 1000;
  RETURN_NOT_OK(RegisterService(queue_limit, master::MakeCDCMasterAdminService(master_)));
  RETURN_NOT_OK(RegisterService(queue_limit, master::MakeCDCMasterClientService(master_)));
  RETURN_NOT_OK(RegisterService(queue_limit, master::MakeCDCMasterDdlService(master_)));
  RETURN_NOT_OK(RegisterService(queue_limit, master::MakeCDCMasterReplicationService(master_)));
  RETURN_NOT_OK(RegisterService(queue_limit, master::MakeCDCMasterClusterService(master_)));

  return Status::OK();
}

void CDCMasterServer::Shutdown() { server::RpcServerBase::Shutdown(); }

}  // namespace cdcserver
}  // namespace yb
