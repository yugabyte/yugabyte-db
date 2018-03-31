//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/processor/pg_processor.h"
#include "yb/yql/pgsql/pbexec/pg_exec_context.h"
#include "yb/common/ql_value.h"

namespace yb {
namespace pgsql {

using std::make_shared;
using std::shared_ptr;
using std::string;

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;

using pgapi::ProtocolVersion;
using pgapi::StringInfo;
using pgapi::PGPort;
using pgapi::PGSend;
using pgapi::PGRecv;
using pgapi::PGInstrOpcode;

//--------------------------------------------------------------------------------------------------
// Class PgProcessor.
// Provides an API for applications to compile and execute PGSQL commands.
//--------------------------------------------------------------------------------------------------

PgProcessor::PgProcessor(const PgEnv::SharedPtr& pg_env) : pg_env_(pg_env) {
}

PgProcessor::~PgProcessor() {
}

CHECKED_STATUS PgProcessor::Process(const PgSession::SharedPtr& client_session,
                                    const pgapi::PGInstr::SharedPtr& instr,
                                    string *exec_status,
                                    string *exec_output) {
  switch (instr->op()) {
    case PGInstrOpcode::kNone: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kBind: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kCancelEOF: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kCopyCompleted: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kCopyData: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kCopyFailed: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kDescribePortal: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kDescribeStatement: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kDropPortal: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kDropStatement: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kExec: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kFlush: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kFunctionCall: FALLTHROUGH_INTENDED;
    case PGInstrOpcode::kParse:
      return STATUS(RuntimeError, "Not yet implemented");
      break;

    case PGInstrOpcode::kCancelX:
      // TODO(neil) At the minimum, pg_session must reset/disconnect current_database.
      DLOG(INFO) << "UnderDevelopment: Canceling connection. Need to revisit this. "
                 << "When cancelling, current_database_ in a session should be set to NULL. "
                 << "When operating on requests, current_database_ in a session should be used "
                 << "to see if client connection is still available, and uncommitted requests "
                 << "should be rolled back.";
      return Status::OK();

    case PGInstrOpcode::kQuery:
      return Run(client_session, static_cast<pgapi::PGInstrQuery *>(instr.get()),
                 exec_status, exec_output);

    case PGInstrOpcode::kSync:
      return STATUS(RuntimeError, "Not yet implemented");
      break;

    case PGInstrOpcode::kStartup:
      return STATUS(RuntimeError, "Unexpected RPC packet. Potential data corruption.");
  }

  return Status::OK();
}

CHECKED_STATUS PgProcessor::Run(const PgSession::SharedPtr& client_session,
                                pgapi::PGInstrQuery *instr,
                                string *exec_status,
                                string *exec_output) {
  const string& stmt = instr->stmt();
  PgCompileContext::SharedPtr compile_context =
    make_shared<PgCompileContext>(pg_env_, client_session, stmt);

  // Parsing.
  RETURN_NOT_OK(parser_.Parse(compile_context));

  // Analyzing.
  RETURN_NOT_OK(analyzer_.Analyze(compile_context));

  // Generating Protobuf.
  PgProto::SharedPtr pg_proto;
  RETURN_NOT_OK(coder_.Generate(compile_context, &pg_proto));

  // Execute code.
  PgExecContext::SharedPtr exec_context =
    make_shared<PgExecContext>(pg_env_, client_session, pg_proto, exec_status, exec_output);
  RETURN_NOT_OK(executor_.Run(exec_context));
  return Status::OK();
}

CHECKED_STATUS PgProcessor::RunTest(const PgSession::SharedPtr& client_session,
                                    const string& stmt) {
  PgCompileContext::SharedPtr compile_context =
    make_shared<PgCompileContext>(pg_env_, client_session, stmt);

  // Parsing.
  RETURN_NOT_OK(parser_.Parse(compile_context));

  // Analyzing.
  RETURN_NOT_OK(analyzer_.Analyze(compile_context));

  // Generating Protobuf.
  PgProto::SharedPtr pg_proto;
  RETURN_NOT_OK(coder_.Generate(compile_context, &pg_proto));

  // Execute code.
  string dbstatus;
  string dboutput;
  PgExecContext::SharedPtr exec_context =
    make_shared<PgExecContext>(pg_env_, client_session, pg_proto, &dbstatus, &dboutput);
  RETURN_NOT_OK(executor_.Run(exec_context));

  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
