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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ybpostgres/pg_instr.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqformat.h"

namespace yb {
namespace pgapi {

using std::make_shared;
using strings::Substitute;

//--------------------------------------------------------------------------------------------------
// Constructor.
PGInstr::PGInstr(const StringInfo& command, PGInstrOpcode op) : command_(command), op_(op) {
}

// kBind
CHECKED_STATUS PGInstr::ReadCommand_B(const StringInfo& command, PGInstr::SharedPtr *instr) {
  *instr = make_shared<PGInstrBind>(command);
  return Status::OK();
}

// kCancelEOF
CHECKED_STATUS PGInstr::ReadCommand_EOF(const StringInfo& command, PGInstr::SharedPtr *instr) {
  *instr = make_shared<PGInstrCancel>(command, PGInstrOpcode::kCancelEOF);
  return Status::OK();
}

// kCancelX
CHECKED_STATUS PGInstr::ReadCommand_X(const StringInfo& command, PGInstr::SharedPtr *instr) {
  *instr = make_shared<PGInstrCancel>(command, PGInstrOpcode::kCancelX);
  return Status::OK();
}

// kCopyCompleted
CHECKED_STATUS PGInstr::ReadCommand_c(const StringInfo& command, PGInstr::SharedPtr *instr) {
  *instr = make_shared<PGInstrCopy>(command, PGInstrOpcode::kCopyCompleted);
  return Status::OK();
}

// kCopyData
CHECKED_STATUS PGInstr::ReadCommand_d(const StringInfo& command, PGInstr::SharedPtr *instr) {
  *instr = make_shared<PGInstrCopy>(command, PGInstrOpcode::kCopyData);
  return Status::OK();
}

// kCopyFailed
CHECKED_STATUS PGInstr::ReadCommand_f(const StringInfo& command, PGInstr::SharedPtr *instr) {
  *instr = make_shared<PGInstrCopy>(command, PGInstrOpcode::kCopyFailed);
  return Status::OK();
}

// kDescribe
CHECKED_STATUS PGInstr::ReadCommand_D(const StringInfo& command, PGInstr::SharedPtr *instr) {
  PGPqFormatter formatter(command);
  const char describe_type = formatter.pq_getmsgbyte();
  switch (describe_type) {
    case 'S':
      *instr = make_shared<PGInstrDescribe>(command,
                                            PGInstrOpcode::kDescribeStatement,
                                            formatter.pq_getmsgstring());
      break;
    case 'P':
      *instr = make_shared<PGInstrDescribe>(command,
                                            PGInstrOpcode::kDescribePortal,
                                            formatter.pq_getmsgstring());
      break;
    default:
      return STATUS(NetworkError,
                    Substitute("invalid DESCRIBE message subtype $0", describe_type));
  }

  return formatter.pq_getmsgend();
}

// kDrop
CHECKED_STATUS PGInstr::ReadCommand_C(const StringInfo& command, PGInstr::SharedPtr *instr) {
  PGPqFormatter formatter(command);

  const char close_type = formatter.pq_getmsgbyte();
  switch (close_type) {
    case 'S':
      // Drop prepared statement.
      *instr = make_shared<PGInstrDrop>(command,
                                        PGInstrOpcode::kDropStatement,
                                        formatter.pq_getmsgstring());
      break;

    case 'P':
      // Drop portal.
      *instr = make_shared<PGInstrDrop>(command,
                                        PGInstrOpcode::kDropPortal,
                                        formatter.pq_getmsgstring());
      break;

    default:
      return STATUS(NetworkError, Substitute("invalid CLOSE message subtype $0", close_type));
      break;
  }

  return formatter.pq_getmsgend();
}

// kExec
CHECKED_STATUS PGInstr::ReadCommand_E(const StringInfo& command, PGInstr::SharedPtr *instr) {
  PGPqFormatter formatter(command);
  *instr = make_shared<PGInstrExec>(command,
                                    formatter.pq_getmsgstring(),
                                    formatter.pq_getmsgint(4));

  return formatter.pq_getmsgend();
}

// kFlush
CHECKED_STATUS PGInstr::ReadCommand_H(const StringInfo& command, PGInstr::SharedPtr *instr) {
  *instr = make_shared<PGInstrFlush>(command);
  return Status::OK();
}

// kFunctionCall
CHECKED_STATUS PGInstr::ReadCommand_F(const StringInfo& command, PGInstr::SharedPtr *instr) {
  return STATUS(NetworkError, "Function call feature is not supported");
}

// kParse
CHECKED_STATUS PGInstr::ReadCommand_P(const StringInfo& command, PGInstr::SharedPtr *instr) {
  PGPqFormatter formatter(command);
  PGInstrParse::SharedPtr pinstr = make_shared<PGInstrParse>(command,
                                                             formatter.pq_getmsgstring(),
                                                             formatter.pq_getmsgstring());
  int param_count = formatter.pq_getmsgint(2);
  for (int i = 0; i < param_count; i++) {
    pinstr->AddParamType(static_cast<PGOid>(formatter.pq_getmsgint(4)));
  }

  *instr = pinstr;
  return formatter.pq_getmsgend();
}

// kQuery
CHECKED_STATUS PGInstr::ReadCommand_Q(const StringInfo& command, PGInstr::SharedPtr *instr) {
  PGPqFormatter formatter(command);
  *instr = make_shared<PGInstrQuery>(command, formatter.pq_getmsgstring());
  return formatter.pq_getmsgend();
}

// kSync
CHECKED_STATUS PGInstr::ReadCommand_S(const StringInfo& command, PGInstr::SharedPtr *instr) {
  *instr = make_shared<PGInstrSync>(command);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PGInstrStartup::PGInstrStartup(const StringInfo& command, ProtocolVersion protocol)
  : PGInstr(command, PGInstrOpcode::kStartup), protocol_(protocol) {
}

//--------------------------------------------------------------------------------------------------

PGInstrBind::PGInstrBind(const StringInfo& command) : PGInstr(command, PGInstrOpcode::kBind) {
}

//--------------------------------------------------------------------------------------------------

PGInstrCancel::PGInstrCancel(const StringInfo& command, PGInstrOpcode op) : PGInstr(command, op) {
}

//--------------------------------------------------------------------------------------------------

PGInstrCopy::PGInstrCopy(const StringInfo& command, PGInstrOpcode op) : PGInstr(command, op) {
}

//--------------------------------------------------------------------------------------------------

PGInstrDescribe::PGInstrDescribe(const StringInfo& command,
                                 PGInstrOpcode op,
                                 const string& target_name)
    : PGInstr(command, op), target_name_(target_name) {
}

//--------------------------------------------------------------------------------------------------

PGInstrDrop::PGInstrDrop(const StringInfo& command,
                         PGInstrOpcode op,
                         const string& target_name)
  : PGInstr(command, op), target_name_(target_name) {
}

//--------------------------------------------------------------------------------------------------

PGInstrExec::PGInstrExec(const StringInfo& command, const string& portal_name, int max_rows)
  : PGInstr(command, PGInstrOpcode::kExec), portal_name_(portal_name), max_rows_(max_rows) {
}

//--------------------------------------------------------------------------------------------------

PGInstrFlush::PGInstrFlush(const StringInfo& command) : PGInstr(command, PGInstrOpcode::kFlush) {
}

//--------------------------------------------------------------------------------------------------

PGInstrParse::PGInstrParse(const StringInfo& command, const string& stmt_name, const string& stmt)
    : PGInstr(command, PGInstrOpcode::kParse),
      stmt_name_(stmt_name),
      stmt_(stmt) {
}

void PGInstrParse::AddParamType(PGOid param_type) {
  param_types_.push_back(param_type);
}

//--------------------------------------------------------------------------------------------------

PGInstrQuery::PGInstrQuery(const StringInfo& command, const string& stmt)
    : PGInstr(command, PGInstrOpcode::kQuery),
      stmt_(stmt) {
}

//--------------------------------------------------------------------------------------------------

PGInstrSync::PGInstrSync(const StringInfo& command) : PGInstr(command, PGInstrOpcode::kSync) {
}

//--------------------------------------------------------------------------------------------------

}  // namespace pgapi
}  // namespace yb
