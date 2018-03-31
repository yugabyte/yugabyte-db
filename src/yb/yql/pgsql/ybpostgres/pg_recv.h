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
#ifndef YB_YQL_PGSQL_YBPOSTGRES_PG_RECV_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PG_RECV_H_

#include "yb/yql/pgsql/ybpostgres/pg_defs.h"
#include "yb/yql/pgsql/ybpostgres/pg_instr.h"
#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqformat.h"

namespace yb {
namespace pgapi {

// This class reads rpc message and return SQL statements.
// It combines the work in "tcop" and "libpq" directories of Postgresql original code.
class PGRecv {
 public:
  PGRecv();
  virtual ~PGRecv() {
  }

  // Set network protocol that should be used when reading.
  void set_protocol_version(ProtocolVersion proto) {
    protocol_version_ = proto;
  }

  // Process connecting packet.
  // Before sending SQL statements, clients set up connection by sending a few messages containing
  // SSL data, protocol, login data, etc.
  CHECKED_STATUS ReadStartupPacket(const IoVecs& data,
                                   size_t start_offset,
                                   size_t *packet_len,
                                   StringInfo *pg_command);

  // Read the command type and its contents.
  CHECKED_STATUS ReadCommand(const IoVecs& data,
                             size_t start_offset,
                             size_t *command_len,
                             StringInfo *pg_command);

  // Translate the command to a database instruction for server to execute.
  CHECKED_STATUS ReadInstr(const StringInfo& command, PGInstr::SharedPtr *instr);

 private:
  ProtocolVersion protocol_version_ = PG_PROTOCOL_LATEST;

  // The following Postgres flags are not yet used.
  bool ignore_till_sync = false;
  bool doing_extended_query_message = false;
};

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PG_RECV_H_
