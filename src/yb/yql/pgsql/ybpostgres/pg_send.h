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
#ifndef YB_YQL_PGSQL_YBPOSTGRES_PG_SEND_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PG_SEND_H_

#include "yb/common/pgsql_resultset.h"

#include "yb/yql/pgsql/ybpostgres/pg_defs.h"
#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqformat.h"
#include "yb/yql/pgsql/ybpostgres/pg_port.h"

namespace yb {
namespace pgapi {

// This class reads rpc message and return SQL statements.
// It combines the work in "tcop" and "libpq" directories of Postgresql original code.
class PGSend {
 public:
  PGSend();
  virtual ~PGSend() {
  }

  // Set network protocol that should be used when writing.
  void set_protocol_version(ProtocolVersion proto) {
    protocol_version_ = proto;
  }

  // Construct the associated network command for the given packet.
  // 1.Pass 0 to "server_status" to avoid ending a server message for the following scenarios.
  //    - Add more messages after this message.
  //    - This message is the ending 'Z' message (The end of the RPC messaging recursive calls).
  // 2.Pass non-zero to "server_status" to end a server message before replying to client.
  //
  // See function "socket_putmessage" in PQcommMethods PqCommSocketMethods.
  void WriteRpcCommand(const StringInfo& packet,
                       char server_status,                        // 'I' for IDLE & ready
                       StringInfo *pg_command) const;

  // All RPC response to client must be ended with the 'Z' server status message.
  // Server status can be one of the following characters.
  //   'I' - Server is IDLE. This means after sending this message, server will wait for new
  //         requests. Without the 'I' ending, client will keep waiting for more messages.
  //   'T' - Server is processing the current request.
  //   'E' - Server is reporting an error.
  //   Others - Server status is unknown.
  void WriteRpcStatus(char server_status, StringInfo *zmsg) const;

  // Write authentication packet.
  void WriteAuthRpcCommand(const PGPort& pgport,
                           string extra_data,
                           StringInfo *auth_command) const;
  void WriteAuthRpcCommand(const PGPort& pgport, StringInfo *auth_command) const {
    return WriteAuthRpcCommand(pgport, "", auth_command);
  }

  // Send a success comment to client.
  void WriteSuccessMessage(const string& comment, StringInfo *pg_command) const;

  // Write error message.
  void WriteErrorMessage(int pgsql_errcode,
                         const string& errmsg,
                         StringInfo *pg_error) const;

  // Tuple
  void WriteTupleDesc(const PgsqlRSRowDesc& tuple_desc, faststring *rows_data);
  void WriteTuples(const PgsqlResultSet& tuples,
                   const PgsqlRSRowDesc& tuple_desc,
                   faststring *rows_data);
  string WriteColumnToString(const QLValue& col_value, const QLType::SharedPtr& col_type);

 private:
  ProtocolVersion protocol_version_ = PG_PROTOCOL_LATEST;

#ifdef POSTGRESQL_NOT_USED
  // The following Postgres flags are not used or not yet
  bool PqCommBusy = false;              /* busy sending data to the client */
  bool PqCommReadingMsg = false;        /* in the middle of reading a message */
  bool DoingCopyOut = false;            /* in old-protocol COPY OUT processing */
#endif
};

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PG_SEND_H_
