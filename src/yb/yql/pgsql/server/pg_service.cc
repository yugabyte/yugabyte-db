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

#include "yb/yql/pgsql/server/pg_service.h"
#include "yb/yql/pgsql/server/pg_rpc.h"
#include "yb/yql/pgsql/ybpostgres/pgsend.h"
#include "yb/yql/pgsql/ybpostgres/pginstr.h"

#ifdef PGSQL_COMPILER_WAS_LANDED
#include "yb/yql/pgsql/processor/pg_processor.h"
#endif

DEFINE_int32(pgsql_rpc_timeout_secs, 5,
             "The number of reactor threads to be used for processing ybclient "
             "requests originating in the PostgreSQL proxy server");

DEFINE_int32(pgsql_ybclient_reactor_threads, 24,
             "The number of reactor threads to be used for processing ybclient "
             "requests originating in the PostgreSQL proxy server");

namespace yb {
namespace pgserver {

using std::make_shared;

using pgapi::PGInstr;
using pgapi::PGInstrOpcode;
using pgapi::PGInstrStartup;
using pgapi::PGInstrStartup;
using pgapi::StringInfo;

using pgsql::PgEnv;
#ifdef PGSQL_COMPILER_WAS_LANDED
using pgsql::PgProcessor;
#endif

//--------------------------------------------------------------------------------------------------
PgServiceImpl::PgServiceImpl(PgServer* server, const PgServerOptions& opts)
    : PgServerServiceIf(server->metric_entity()),
      async_client_init_("pgsql_ybclient",
                         FLAGS_pgsql_ybclient_reactor_threads,
                         FLAGS_pgsql_rpc_timeout_secs,
                         server->tserver() ? server->tserver()->permanent_uuid() : "",
                         &opts, server->metric_entity()),
      pg_env_(std::make_shared<PgEnv>(async_client_init_.client())) {
}

PgServiceImpl::~PgServiceImpl() {
}

void PgServiceImpl::Handle(yb::rpc::InboundCallPtr call_ptr) {
  // Get Postgresql instruction.
  PgInboundCall::SharedPtr pgcall = std::static_pointer_cast<PgInboundCall>(call_ptr);
  const pgapi::PGInstr::SharedPtr& instr = pgcall->instr();

  if (pgcall->executed()) {
    // Skip the command as it was already executed during the parsing step.
    pgcall->SkipExecution(true);
    return;
  }

  if (instr->op() == PGInstrOpcode::kStartup) {
    // For starup packet, execute here to construct a session before using the processor.
    PGInstrStartup::SharedPtr startup = std::static_pointer_cast<PGInstrStartup>(pgcall->instr());
    Status s = pgcall->ProcessStartupPacket(pg_env_, startup->packet(), startup->protocol());
    CHECK(s.ok()) << "Error handling is not yet implemented for startup packet";
    return;
  }

  // TODO(neil) Need to use a processor pool.
  // - PgProcessor is not a thread, but it might have large objects (parser, analyzer,
  //   executor, ...).  By reusing the processor, we don't have to reconstruct these large objects.
  //
  // - Total number of processors need to be under control. The number is dependent on how many
  //   requests can YB server handle at one time. When the limit is reached, we can send postgresql
  //   client a busy status. The clients will not send requests until they received an IDLE status.
  //
  // - For now, keep the following code until supported features are tested.
#ifdef PGSQL_COMPILER_WAS_LANDED
  string exec_status;
  PgProcessor::SharedPtr processor = make_shared<PgProcessor>(pg_env_);
  Status s = processor->Process(pgcall->pg_session(), instr, &exec_status);
  if (PREDICT_TRUE(s.ok())) {
    pgcall->RespondSuccess(exec_status);
  } else {
    pgcall->RespondFailure(rpc::ErrorStatusPB::ERROR_APPLICATION, s);
  }
#endif
}

const std::shared_ptr<client::YBClient>& PgServiceImpl::client() const {
  return async_client_init_.client();
}

}  // namespace pgserver
}  // namespace yb
