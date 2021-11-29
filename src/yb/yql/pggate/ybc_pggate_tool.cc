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

#include "yb/yql/pggate/ybc_pggate_tool.h"

#include "yb/common/ybc-internal.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/server_base_options.h"

#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/shared_mem.h"

#include "yb/yql/pggate/pg_env.h"
#include "yb/yql/pggate/pggate.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pggate.h"

namespace yb {
namespace pggate {

namespace {

// Fetches relation's unique constraint name to specified buffer.
// If relation is not an index and it has primary key the name of primary key index is returned.
// In other cases, relation name is used.
//
// Not implemented for tools.
void FetchUniqueConstraintName(PgOid relation_id, char* dest, size_t max_size) {
  CHECK(false) << "Not implemented";
}

YBCPgMemctx GetCurrentToolYbMemctx() {
  static YBCPgMemctx tool_memctx = nullptr;

  if (!tool_memctx) {
    tool_memctx = YBCPgCreateMemctx();
  }
  return tool_memctx;
}

// Conversion Table.
// Contain function pointers for conversion between PostgreSQL Datum to YugaByte data.
// Currently it is not used in the tools and can be empty.
static const YBCPgTypeEntity YBCEmptyTypeEntityTable[] = {};

CHECKED_STATUS PrepareInitPgGateBackend() {
  server::MasterAddresses master_addresses;
  std::string resolved_str;
  RETURN_NOT_OK(server::DetermineMasterAddresses(
      "pggate_master_addresses", FLAGS_pggate_master_addresses, 0, &master_addresses,
      &resolved_str));
  LOG(INFO) << "Master addresses: " << AsString(master_addresses);

  PgApiContext context;
  struct Data {
    boost::optional<tserver::TServerSharedObject> tserver_shared_object;
    HostPort reached_host_port;
    std::atomic<bool> flag{false};
    CountDownLatch latch{1};
    std::atomic<size_t> running{0};
    Status failure;
  };
  static std::shared_ptr<Data> data = std::make_shared<Data>();
  data->tserver_shared_object.emplace(VERIFY_RESULT(tserver::TServerSharedObject::Create()));
  data->running = 0;
  for (const auto& list : master_addresses) {
    data->running += list.size();
  }
  for (const auto& list : master_addresses) {
    for (const auto& host_port : list) {
      tserver::TabletServerServiceProxy proxy(context.proxy_cache.get(), host_port);
      struct ReqData {
        tserver::GetSharedDataRequestPB req;
        tserver::GetSharedDataResponsePB resp;
        rpc::RpcController controller;
      };
      auto req_data = std::make_shared<ReqData>();
      req_data->controller.set_timeout(std::chrono::seconds(60));
      proxy.GetSharedDataAsync(
          req_data->req, &req_data->resp, &req_data->controller,
          [req_data, host_port = host_port] {
        if (req_data->controller.status().ok()) {
          bool expected = false;
          if (data->flag.compare_exchange_strong(expected, true)) {
            memcpy(pointer_cast<char*>(&**data->tserver_shared_object),
                   req_data->resp.data().c_str(), req_data->resp.data().size());
            data->reached_host_port = host_port;
            data->latch.CountDown();
          }
        } else if (--data->running == 0) {
          data->failure = req_data->controller.status();
          data->latch.CountDown();
        }
      });
    }
  }

  data->latch.Wait();
  RETURN_NOT_OK(data->failure);

  FLAGS_pggate_tserver_shm_fd = data->tserver_shared_object->GetFd();

  auto& shared_data = **data->tserver_shared_object;
  shared_data.SetHostEndpoint(shared_data.endpoint(), data->reached_host_port.host());
  LOG(INFO) << "Shared data fetched, endpoint: " << shared_data.endpoint()
            << ", host: " << shared_data.host().ToBuffer()
            << ", catalog version: " << shared_data.ysql_catalog_version()
            << ", postgres_auth_key: " << shared_data.postgres_auth_key();

  YBCPgCallbacks callbacks;
  callbacks.FetchUniqueConstraintName = &FetchUniqueConstraintName;
  callbacks.GetCurrentYbMemctx = &GetCurrentToolYbMemctx;
  YBCInitPgGateEx(YBCEmptyTypeEntityTable, 0, callbacks, &context);

  return Status::OK();
}

} // anonymous namespace

//--------------------------------------------------------------------------------------------------
// C API.
//--------------------------------------------------------------------------------------------------
extern "C" {

void YBCSetMasterAddresses(const char* hosts) {
  LOG(INFO) << "Setting custom master addresses: " << hosts;
  FLAGS_pggate_master_addresses = hosts;
}

YBCStatus YBCInitPgGateBackend() {
  auto status = PrepareInitPgGateBackend();
  if (!status.ok()) {
    return ToYBCStatus(status);
  }
  return YBCPgInitSession(/* pg_env */ nullptr, /* database_name */ nullptr);
}

void YBCShutdownPgGateBackend() {
  YBCDestroyPgGate();
  YBCPgDestroyMemctx(GetCurrentToolYbMemctx());
}

} // extern "C"

} // namespace pggate
} // namespace yb
