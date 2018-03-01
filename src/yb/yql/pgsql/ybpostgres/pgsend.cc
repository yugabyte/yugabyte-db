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

#include "yb/yql/pgsql/ybpostgres/pgsend.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"

namespace yb {
namespace pgapi {

using strings::Substitute;

//--------------------------------------------------------------------------------------------------
// Constructor.
PGSend::PGSend() {
}

//--------------------------------------------------------------------------------------------------

// See function "socket_putmessage" in PQcommMethods PqCommSocketMethods.
void PGSend::WriteRpcCommand(const StringInfo& packet,
                             char server_status,
                             StringInfo *pg_command) const {
  *pg_command = nullptr;

  int msgtype = packet->content_type;
  StringInfo command;
  initStringInfo(&command, msgtype);

  // Write command type.
  appendStringInfoChar(command, static_cast<char>(msgtype));

  // Write command length.
  if (PG_PROTOCOL_MAJOR(protocol_version_) >= 3) {
    uint32_t len = packet->size() + 4;
    uint32_t n32 = htonl(len);
    appendBinaryStringInfo(command, reinterpret_cast<char *>(&n32), 4);
  }

  // Write content.
  appendBinaryStringInfo(command, packet->buffer(), packet->size());

  // Write the status.
  if (server_status != 0) {
    StringInfo zpacket = pgapi::makeStringInfo(static_cast<int>('Z'));
    pgapi::appendStringInfoChar(zpacket, server_status);

    StringInfo zmsg;
    WriteRpcCommand(zpacket, 0, &zmsg);
    command->data.append(zmsg->data);
  }

  *pg_command = command;
}

//--------------------------------------------------------------------------------------------------

void PGSend::WriteAuthRpcCommand(const PGPort& pgport,
                                 string extra_data,
                                 StringInfo *auth_command) const {
  // Accept all clients without any authentication.
  int32_t areq = static_cast<int32_t>(AuthRequest::AUTH_REQ_OK);
  PGPqFormatter formatter;
  formatter.pq_beginmessage('R');
  formatter.pq_sendint(areq, sizeof(uint32_t));
  if (!extra_data.empty()) {
    formatter.pq_sendbytes(extra_data.c_str(), extra_data.length());
  }

  // Now write the packet.
  WriteRpcCommand(formatter.send_buf(), 0, auth_command);
}

//--------------------------------------------------------------------------------------------------
void PGSend::WriteSuccessMessage(const string& comment,
                                 StringInfo *pg_command) const {
  PGPqFormatter formatter;
  formatter.pq_beginmessage('C');
  formatter.pq_sendbytes(comment.c_str(), comment.size());
  formatter.pq_sendbyte('\0');

  // Now write the packet.
  WriteRpcCommand(formatter.send_buf(), 'I', pg_command);
}

//--------------------------------------------------------------------------------------------------
// See function "send_message_to_frontend(ErrorData *edata)" in postgresql::elog.c.
void PGSend::WriteErrorMessage(int pgsql_errcode,
                               const string& errmsg,
                               StringInfo *pg_error) const {
  PGPqFormatter formatter;
  formatter.pq_beginmessage('E');

  if (PG_PROTOCOL_MAJOR(protocol_version_) >= 3) {
    // Error code.
    int i;
    int ssval = MAKE_SQLSTATE('5', '7', '0', '1', '4');
    char tbuf[12];
    for (i = 0; i < 5; i++) {
      tbuf[i] = PGUNSIXBIT(ssval);
      ssval >>= 6;
    }
    tbuf[i] = '\0';
    formatter.pq_sendbyte(PG_DIAG_SQLSTATE);
    formatter.pq_sendstring(tbuf);

    // Error message;
    formatter.pq_sendbyte(PG_DIAG_MESSAGE_PRIMARY);
    formatter.pq_sendstring(errmsg.c_str());
    formatter.pq_sendbyte('\0');
  } else {
    string msg = ": : ";
    msg += errmsg + string("\n");
    formatter.pq_sendstring(errmsg.c_str());
  }

  // Now write the RPC error message.
  WriteRpcCommand(formatter.send_buf(), 'I', pg_error);
}

}  // namespace pgapi
}  // namespace yb
