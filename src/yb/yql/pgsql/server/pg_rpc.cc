//
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
//

#include "yb/yql/pgsql/server/pg_rpc.h"
#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/size_literals.h"

using namespace std::literals; // NOLINT
using namespace std::placeholders;
using namespace yb::size_literals; // NOLINT

using std::make_shared;
using strings::Substitute;
using yb::operator"" _KB;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);
DEFINE_uint64(pgsql_max_concurrent_commands, 1,
              "Max number of PostgreSQL commands received from single connection, "
              "that could be processed concurrently");
DEFINE_int32(rpcz_max_pgsql_query_dump_size, 4_KB,
             "The maximum size of the PostgreSQL query string in the RPCZ dump.");
DEFINE_int32(rpcz_max_pgsql_batch_dump_count, 4_KB,
             "The maximum number of PostgreSQL batch elements in the RPCZ dump.");
DEFINE_uint64(pgsql_max_queued_bytes, 16_MB,
              "Max number of bytes in queued PostgreSQL commands.");

namespace yb {
namespace pgserver {

// Postgresql interface datatype. We implemented as StringInfoData::SharedPtr.
using pgapi::AuthRequest;
using pgapi::PGInstr;
using pgapi::PGInstrStartup;
using pgapi::PGPort;
using pgapi::PGPqFormatter;
using pgapi::ProtocolVersion;
using pgapi::StringInfo;

using pgsql::PgSession;
using pgsql::PgEnv;

//--------------------------------------------------------------------------------------------------
// Class PgConnectionContext.
//--------------------------------------------------------------------------------------------------
PgConnectionContext::PgConnectionContext()
  : ConnectionContextWithQueue(FLAGS_pgsql_max_concurrent_commands,
                               FLAGS_pgsql_max_queued_bytes) {
}

PgConnectionContext::~PgConnectionContext() {
}

//--------------------------------------------------------------------------------------------------

Result<size_t> PgConnectionContext::ProcessCalls(const rpc::ConnectionPtr& connection,
                                                 const IoVecs& data,
                                                 rpc::ReadBufferFull read_buffer_full) {
  size_t total_bytes = IoVecsFullSize(data);
  size_t consumed = 0;

  while (consumed < total_bytes) {
    // TODO(neil) Not sure what to do with network error yet.
    size_t command_bytes = 0;
    StringInfo postgres_command;
    pgapi::PGInstr::SharedPtr pginstr;

    if (state_ == rpc::RpcConnectionPB::NEGOTIATING) {
      // Read connecting package.
      RETURN_NOT_OK(pgrecv_.ReadStartupPacket(data, consumed, &command_bytes, &postgres_command));
      if (command_bytes == 0) {
        break;
      }
      // Process the connecting packet.
      RETURN_NOT_OK(ProcessStartupPacket(connection, postgres_command, command_bytes, &pginstr));

    } else {
      // Read rpc command.
      RETURN_NOT_OK(pgrecv_.ReadCommand(data, consumed, &command_bytes, &postgres_command));
      if (command_bytes == 0) {
        break;
      }
      // Analyze the command to extract the SQL statement.
      RETURN_NOT_OK(pgrecv_.ReadInstr(postgres_command, &pginstr));
    }

    // Queue the command.
    RETURN_NOT_OK(HandleInboundCall(connection, command_bytes, pginstr));
  }
  return consumed;
}

CHECKED_STATUS PgConnectionContext::HandleInboundCall(const rpc::ConnectionPtr& connection,
                                                      size_t command_bytes,
                                                      const pgapi::PGInstr::SharedPtr& instr) {
  auto reactor = connection->reactor();
  DCHECK(reactor->IsCurrentThread());

  // Queue the call for execution.
  auto call = std::make_shared<PgInboundCall>(connection, this, call_processed_listener(),
                                              command_bytes, instr);
  Enqueue(std::move(call));

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgConnectionContext::ProcessStartupPacket(const rpc::ConnectionPtr& connection,
                                                         const StringInfo& postgres_packet,
                                                         size_t command_bytes,
                                                         PGInstr::SharedPtr *instr) {
  // The first field is either a protocol version number or a special request code.
  const ProtocolVersion *buf =
    reinterpret_cast<const ProtocolVersion *>(postgres_packet->data.c_str());
  const ProtocolVersion proto = ntohl(*buf);

  // Process cancelling command.
  if (proto == CANCEL_REQUEST_CODE) {
    // TODO(neil) Need to work on cancelling all queued operations and close out this connection.
    // This would be the last step of the SQL project.

    /* Not really an error, but we don't want to proceed further */
    return STATUS(NetworkError, "Canceled.");
  }

  // Process SSL option. This must be done before any actual client statement is read.
  if (proto == NEGOTIATE_SSL_CODE && !doneSSL) {
    // Reply that SSL is not supported with "N".
    LOG(INFO) << "Not supporting SSL for now. Returning 'N'";
    Enqueue(std::make_shared<PgInboundCall>(connection, this, call_processed_listener(),
                                            command_bytes, "N"));
    doneSSL = true;
    return Status::OK();
  }

  // Process startup packet and setup ConnectionContext to be used later.
  // - Check client protocol version.
  // - Collect information from the startup packet into PGPort "pgport_".
  // - Update reader and sender with correct protocol.
  // - Send authentication request to client. However, currently we accept all clients.

  // TODO(neil) Should return error message to client instead of crashing.
  if (PG_PROTOCOL_MAJOR(proto) < PG_PROTOCOL_MAJOR(PG_PROTOCOL_EARLIEST) ||
      PG_PROTOCOL_MAJOR(proto) > PG_PROTOCOL_MAJOR(PG_PROTOCOL_LATEST) ||
      (PG_PROTOCOL_MAJOR(proto) == PG_PROTOCOL_MAJOR(PG_PROTOCOL_LATEST) &&
       PG_PROTOCOL_MINOR(proto) > PG_PROTOCOL_MINOR(PG_PROTOCOL_LATEST))) {
    return STATUS(NetworkError,
                  Substitute("Unsupported frontend protocol $0.$1: server supports $2.$3 to $4.$5",
                             PG_PROTOCOL_MAJOR(proto),
                             PG_PROTOCOL_MINOR(proto),
                             PG_PROTOCOL_MAJOR(PG_PROTOCOL_EARLIEST),
                             PG_PROTOCOL_MINOR(PG_PROTOCOL_EARLIEST),
                             PG_PROTOCOL_MAJOR(PG_PROTOCOL_LATEST),
                             PG_PROTOCOL_MINOR(PG_PROTOCOL_LATEST)));
  }

  // Collect data from the packet and saved them in PgSession.
  *instr = make_shared<PGInstrStartup>(postgres_packet, proto);
  return Status::OK();
}

CHECKED_STATUS PgConnectionContext::CreateSession(const PgEnv::SharedPtr& pg_env,
                                                  const StringInfo& postgres_packet,
                                                  ProtocolVersion protocol) {
  pg_session_ = make_shared<PgSession>(pg_env, postgres_packet, protocol);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Class PgInboundCall.
//--------------------------------------------------------------------------------------------------
PgInboundCall::PgInboundCall(rpc::ConnectionPtr conn,
                             PgConnectionContext *conn_context,
                             CallProcessedListener call_processed_listener,
                             size_t command_bytes,
                             const pgapi::PGInstr::SharedPtr& instr)
    : QueueableInboundCall(std::move(conn), command_bytes, std::move(call_processed_listener)),
      instr_(instr),
      conn_context_(conn_context) {
}

PgInboundCall::PgInboundCall(rpc::ConnectionPtr conn,
                             PgConnectionContext *conn_context,
                             CallProcessedListener call_processed_listener,
                             size_t command_bytes,
                             const Slice& response)
    : QueueableInboundCall(std::move(conn), command_bytes, std::move(call_processed_listener)),
      response_(response.data(), response.size()),
      executed_(true),
      conn_context_(conn_context) {
}

//--------------------------------------------------------------------------------------------------
CHECKED_STATUS PgInboundCall::ProcessStartupPacket(const PgEnv::SharedPtr& pg_env,
                                                   const StringInfo& startup_packet,
                                                   ProtocolVersion proto) {
  // Create a session for this connection.
  RETURN_NOT_OK(conn_context_->CreateSession(pg_env, startup_packet, proto));

  // Open the server for accepting query requests.
  conn_context_->set_state(rpc::RpcConnectionPB::OPEN);

  // Write a dummy authentication request as we accept all clients for now.
  StringInfo auth_msg;
  conn_context_->pgsend().WriteAuthRpcCommand(conn_context_->pgport(), &auth_msg);

  // TODO(neil) We suppose to move on to authentication here.  Skip it for now.
  // Write "Z" message to instruct the client that server is idle and ready to accept requests.
  StringInfo zmsg;
  StringInfo zpacket = pgapi::makeStringInfo(static_cast<int>('Z'));
  pgapi::appendStringInfoChar(zpacket, 'I');
  conn_context_->pgsend().WriteRpcCommand(zpacket, 0, &zmsg);

  // Send away the auth_msg and zmsg to instruct clients server is ready.
  string rpcmsg = auth_msg->data + zmsg->data;
  Respond(rpcmsg, true);

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
const std::string& PgInboundCall::service_name() const {
  static std::string result = "yb.pgserver.PgServerService"s;
  return result;
}

const std::string& PgInboundCall::method_name() const {
  static std::string result = "ExecuteRequest"s;
  return result;
}

MonoTime PgInboundCall::GetClientDeadline() const {
  // TODO(neil) Postgresql does set timeout for connection keep-alive.
  // Allow no timeout for now.
  return MonoTime::Max();
}

//--------------------------------------------------------------------------------------------------
// Sending response_ message away.
void PgInboundCall::Serialize(std::deque<RefCntBuffer>* output) const {
  output->push_back(response_);
}

//--------------------------------------------------------------------------------------------------
// Caching responses to response_.
void PgInboundCall::RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code,
                                   const Status& status) {
  // Write error message.
  StringInfo pg_msg;
  conn_context_->pgsend().WriteErrorMessage(-1, status.ToString(), &pg_msg);
  Respond(pg_msg->data, false);
}

void PgInboundCall::RespondSuccess(const string& msg) {
  // Write error message.
  StringInfo pg_msg;
  conn_context_->pgsend().WriteSuccessMessage(msg, &pg_msg);
  Respond(pg_msg->data, true);
}

void PgInboundCall::Respond(const Slice& msg, bool succeeded) {
  response_ = RefCntBuffer(msg.data(), msg.size());
  RecordHandlingCompleted(nullptr);
  QueueResponse(succeeded);
}

void PgInboundCall::SkipExecution(bool succeeded) {
  if (has_response()) {
    RecordHandlingCompleted(nullptr);
    QueueResponse(succeeded);
  }
}

//--------------------------------------------------------------------------------------------------
bool PgInboundCall::DumpPB(const rpc::DumpRunningRpcsRequestPB& req,
                           rpc::RpcCallInProgressPB* resp) {
  return true;
}

} // namespace pgserver
} // namespace yb
