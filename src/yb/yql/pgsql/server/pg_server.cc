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

#include "yb/yql/pgsql/server/pg_server.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/util/flag_tags.h"
#include "yb/util/size_literals.h"

#include "yb/yql/pgsql/server/pg_rpc.h"
#include "yb/yql/pgsql/server/pg_service.h"

using yb::rpc::ServiceIf;
using namespace yb::size_literals;

DEFINE_int32(pgsql_service_queue_length, 1000, "RPC queue length for PostgreSQL service");
TAG_FLAG(pgsql_service_queue_length, advanced);

DEFINE_int64(pg_rpc_block_size, 1_MB, "Redis RPC block size");
DEFINE_int64(pg_rpc_memory_limit, 2_GB, "Redis RPC memory limit");

namespace yb {
namespace pgserver {

PgServer::PgServer(const PgServerOptions& opts, const tserver::TabletServer* const tserver)
    : RpcAndWebServerBase(
          "PgServer", opts, "yb.pgserver",
          std::make_shared<rpc::ConnectionContextFactoryImpl<PgConnectionContext>>(
              FLAGS_pg_rpc_block_size, FLAGS_pg_rpc_memory_limit,
              MemTracker::CreateTracker("PG", tserver ? tserver->mem_tracker() : nullptr))),
      opts_(opts), tserver_(tserver) {
}

Status PgServer::Start() {
  RETURN_NOT_OK(server::RpcAndWebServerBase::Init());

  std::unique_ptr<ServiceIf> pgsql_service(new PgServiceImpl(this, opts_));
  RETURN_NOT_OK(RegisterService(FLAGS_pgsql_service_queue_length, std::move(pgsql_service)));

  RETURN_NOT_OK(server::RpcAndWebServerBase::Start());

  return Status::OK();
}

CHECKED_STATUS PgServer::KeepAlive() {
  // TODO(neil) Should return error status to shutdown server when necessary.
  // Return OK() periodically for now.
  SleepFor(MonoDelta::FromSeconds(60));
  return Status::OK();
}

void PgServer::Shutdown() {
  server::RpcAndWebServerBase::Shutdown();
}

}  // namespace pgserver
}  // namespace yb
