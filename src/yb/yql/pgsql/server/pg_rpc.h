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
#ifndef YB_YQL_PGSQL_SERVER_PG_RPC_H
#define YB_YQL_PGSQL_SERVER_PG_RPC_H

#include "yb/rpc/connection.h"
#include "yb/rpc/rpc_with_queue.h"
#include "yb/yql/pgsql/ybpostgres/pgport.h"
#include "yb/yql/pgsql/ybpostgres/pgrecv.h"
#include "yb/yql/pgsql/ybpostgres/pgsend.h"

#include "yb/yql/pgsql/util/pg_session.h"

namespace yb {
namespace pgserver {

class PgConnectionContext : public rpc::ConnectionContextWithQueue {
 public:
  PgConnectionContext(
      const MemTrackerPtr& read_buffer_tracker,
      const MemTrackerPtr& call_tracker);
  virtual ~PgConnectionContext();

  //------------------------------------------------------------------------------------------------
  Result<size_t> ProcessCalls(const rpc::ConnectionPtr& connection,
                              const IoVecs& data,
                              rpc::ReadBufferFull read_buffer_full) override;

  CHECKED_STATUS HandleInboundCall(const rpc::ConnectionPtr& connection,
                                   size_t command_bytes,
                                   const pgapi::PGInstr::SharedPtr& instr);

  //------------------------------------------------------------------------------------------------
  // Process negociating/startup packet and initialize the connection context using the received
  // data in the startup packet.
  // NOTE:
  // * Do not change this function name. Although the implementation is different, the effect is
  //   exactly the same as Postgresql's function of the same name in file
  //   "/src/backend/postmaster/postmaster.c". If postgres changes its behavior, we'll have to
  //   update this function accordingly.
  // * The following is the description from Postgres for function "ProcessStartupPacket()".
  //   - Read a client's startup packet and do something according to it.
  //
  //   - Returns STATUS_OK or STATUS_ERROR, or might call ereport(FATAL) and
  //     not return at all.
  //
  //   - (Note that ereport(FATAL) stuff is sent to the client, so only use it
  //     if that's what you want.  Return STATUS_ERROR if you don't want to
  //     send anything to the client, which would typically be appropriate
  //     if we detect a communications failure.)
  CHECKED_STATUS ProcessStartupPacket(const rpc::ConnectionPtr& connection,
                                      const pgapi::StringInfo& postgres_packet,
                                      size_t command_bytes,
                                      pgapi::PGInstr::SharedPtr *instr);

  // Use the information in the startup packet to create the session for this connection.
  // Once the connection is gone, the session is gone with it.
  CHECKED_STATUS CreateSession(const pgsql::PgEnv::SharedPtr& pg_env,
                               const pgapi::StringInfo& postgres_packet,
                               pgapi::ProtocolVersion proto);

  //------------------------------------------------------------------------------------------------
  void Connected(const rpc::ConnectionPtr& connection) override { }

  rpc::RpcConnectionPB::StateType State() override {
    return state_;
  }

  void set_state(rpc::RpcConnectionPB::StateType st) {
    state_ = st;
  }

  size_t BufferLimit() override {
    // return PgMessage::kMaxMessageLength;
    return 4098;
  }

  const pgapi::PGRecv& pgrecv() const {
    return pgrecv_;
  }

  const pgsql::PgSession::SharedPtr& pg_session() const {
    return pg_session_;
  }

  const pgapi::PGSend& pgsend() const {
    return pg_session_->pgsend();
  }

  const pgapi::PGPort& pgport() const {
    return pg_session_->pgport();
  }

 private:
  //------------------------------------------------------------------------------------------------
  rpc::RpcConnectionPB::StateType state_ = rpc::RpcConnectionPB::NEGOTIATING;

  // Postgres connection data.
  bool doneSSL = false;
  pgapi::PGRecv pgrecv_;
  pgsql::PgSession::SharedPtr pg_session_;
};

class PgInboundCall : public rpc::QueueableInboundCall {
 public:
  typedef std::shared_ptr<PgInboundCall> SharedPtr;

  // Constructor & destructor.
  PgInboundCall(rpc::ConnectionPtr conn,
                PgConnectionContext *conn_context,
                CallProcessedListener call_processed_listener,
                size_t command_bytes,
                const pgapi::PGInstr::SharedPtr& instr);
  PgInboundCall(rpc::ConnectionPtr conn,
                PgConnectionContext *conn_context,
                CallProcessedListener call_processed_listener,
                size_t command_bytes,
                const Slice& response);
  virtual ~PgInboundCall() { }

  // Recording errors.
  void RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code, const Status& status) override;

  void RespondSuccess(const string& msg);

  // All respond with end with these functions.
  void Respond(const Slice& msg, bool succeeded = true);

  // Skip the execution and send out the response if any.
  void SkipExecution(bool succeeded);

  // Sending response.
  void Serialize(std::deque<RefCntBuffer>* output) const override;

  // Connection properties.
  const std::string& service_name() const override;
  const std::string& method_name() const override;
  MonoTime GetClientDeadline() const override;

  bool has_response() const {
    return executed_ && !response_.empty();
  }

  bool executed() const {
    return executed_;
  }

  const pgsql::PgSession::SharedPtr& pg_session() {
    return conn_context_->pg_session();
  }

  // Startup.
  CHECKED_STATUS ProcessStartupPacket(const pgsql::PgEnv::SharedPtr& pg_env,
                                      const pgapi::StringInfo& postgres_packet,
                                      pgapi::ProtocolVersion proto);

  // Instruction access.
  const pgapi::PGInstr::SharedPtr& instr() {
    return instr_;
  }

  // Logging.
  bool DumpPB(const rpc::DumpRunningRpcsRequestPB& req, rpc::RpcCallInProgressPB* resp) override;
  void LogTrace() const override {
  }
  std::string ToString() const override {
    return instr_->ToString();
  }

 private:
  // Database instruction. Rpc layer construct the instruction which will be compiled and executed.
  pgapi::PGInstr::SharedPtr instr_;

  // Cache responses here when executing processes call Respond().
  // Return the cached message when RPC calls Serialize() for outbound messages.
  RefCntBuffer response_;

  // True if response_ has been written, and no execution is needed.
  bool executed_ = false;

  // Hold connection context here.
  PgConnectionContext *conn_context_;
};

} // namespace pgserver
} // namespace yb

#endif // YB_YQL_PGSQL_SERVER_PG_RPC_H
