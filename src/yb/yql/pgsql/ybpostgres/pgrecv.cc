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

#include "yb/yql/pgsql/ybpostgres/pgrecv.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"

using std::make_shared;

namespace yb {
namespace pgapi {

using strings::Substitute;

//--------------------------------------------------------------------------------------------------
// Constructor.
PGRecv::PGRecv() {
}

//--------------------------------------------------------------------------------------------------
CHECKED_STATUS PGRecv::ReadStartupPacket(const IoVecs& data,
                                         size_t start_offset,
                                         size_t *scan_len,
                                         StringInfo *pg_packet) {
  // Initialize output to null.
  *pg_packet = nullptr;
  *scan_len = 0;

  PGPqCommandReader reader(data, start_offset);
  StringInfo packet;
  initStringInfo(&packet);

  // Read packet len.
  int32_t len;
  if (reader.pq_getint32(&len) == EOF) {
    return STATUS(NetworkError, "Incomplete startup packet");
  }
  if (len < sizeof(ProtocolVersion) || len > MAX_STARTUP_PACKET_LENGTH) {
    return STATUS(NetworkError, "Invalid length of startup packet");
  }

  // Read the packet contents. If there isn't enough data, return EOF and wait for the rest of the
  // data to arrive from the network.
  if (reader.pq_getbytes(packet, len - 4) == EOF) {
    return Status::OK();
  }

  // Return the packet.
  *scan_len = reader.scan_length();
  *pg_packet = packet;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
CHECKED_STATUS PGRecv::ReadCommand(const IoVecs& data,
                                   size_t start_offset,
                                   size_t *scan_len,
                                   StringInfo *pg_command) {
  // Initialize output to null.
  *pg_command = nullptr;
  *scan_len = 0;

  PGPqCommandReader reader(data, start_offset);
  StringInfo command;
  initStringInfo(&command);

  const int qtype = reader.pq_getbyte();
  if (qtype == EOF) {
    return Status::OK();
  }

  // Validate message type code before trying to read body; if we have lost
  // sync, better to say "command unknown" than to run out of memory because
  // we used garbage as a length word.
  //
  // This also gives us a place to set the doing_extended_query_message flag
  // as soon as possible.
  switch (qtype) {
    case 'Q':                           /* simple query */
      doing_extended_query_message = false;
      if (PG_PROTOCOL_MAJOR(protocol_version_) < 3) {
        /* old style without length word; convert */
        if (reader.pq_getstring(command)) {
          // Reach EOF on client message. Skip it.
          return Status::OK();
        }
      }
      break;

    case 'F':                           /* fastpath function call */
      doing_extended_query_message = false;
      if (PG_PROTOCOL_MAJOR(protocol_version_) < 3) {
        // We don't support old function.
        // GetOldFunctionMessage(command);
        return STATUS(NetworkError, "unexpected call on client connection");
      }
      break;

    case 'X':                           /* terminate */
      doing_extended_query_message = false;
      ignore_till_sync = false;
      break;

    case 'B': FALLTHROUGH_INTENDED;                          /* bind */
    case 'C': FALLTHROUGH_INTENDED;                          /* close */
    case 'D': FALLTHROUGH_INTENDED;                          /* describe */
    case 'E': FALLTHROUGH_INTENDED;                          /* execute */
    case 'H': FALLTHROUGH_INTENDED;                          /* flush */
    case 'P':                                                /* parse */
      doing_extended_query_message = true;
      /* these are only legal in protocol 3 */
      if (PG_PROTOCOL_MAJOR(protocol_version_) < 3) {
        return STATUS(NetworkError, Substitute("invalid frontend message type $0", qtype));
      }
      break;

    case 'S':                           /* sync */
      /* stop any active skip-till-Sync */
      ignore_till_sync = false;
      /* mark not-extended, so that a new error doesn't begin skip */
      doing_extended_query_message = false;
      /* only legal in protocol 3 */
      if (PG_PROTOCOL_MAJOR(protocol_version_) < 3) {
        return STATUS(NetworkError, Substitute("invalid frontend message type $0", qtype));
      }
      break;

    case 'd': FALLTHROUGH_INTENDED;                          /* copy data */
    case 'c': FALLTHROUGH_INTENDED;                          /* copy done */
    case 'f':                                                /* copy fail */
      doing_extended_query_message = false;
      /* these are only legal in protocol 3 */
      if (PG_PROTOCOL_MAJOR(protocol_version_) < 3) {
        return STATUS(NetworkError, Substitute("invalid frontend message type $0", qtype));
      }
      break;

    default:
      // Otherwise we got garbage from the frontend.  We treat this as
      // fatal because we have probably lost message boundary sync, and
      // there's no good way to recover.
      return STATUS(NetworkError,
                    Substitute("invalid frontend message type $0", qtype));
  }

  /*
   * In protocol version 3, all frontend messages have a length word next
   * after the type code; we can read the message contents independently of
   * the type.
   */
  if (PG_PROTOCOL_MAJOR(protocol_version_) >= 3) {
    RETURN_NOT_OK(reader.pq_getmessage(command, 0));
  }

  // Return the type of the message.
  *scan_len = reader.scan_length();
  command->content_type = qtype;
  *pg_command = command;
  return Status::OK();
}

CHECKED_STATUS PGRecv::ReadInstr(const StringInfo& command, PGInstr::SharedPtr *instr) {
  // TODO(neil) Must set instruction timestamp in INSTR.
  *instr = nullptr;
  switch (command->content_type) {
    case 'Q':
      // Query (SQL statement that is not in a transaction).
      RETURN_NOT_OK(PGInstr::ReadCommand_Q(command, instr));
      break;

    case 'P': {
      // Parsing.
      RETURN_NOT_OK(PGInstr::ReadCommand_P(command, instr));
      break;
    }

    case 'B': {
      RETURN_NOT_OK(PGInstr::ReadCommand_B(command, instr));
      break;
    }

    case 'E':
      RETURN_NOT_OK(PGInstr::ReadCommand_E(command, instr));
      break;

    case 'F':
      RETURN_NOT_OK(PGInstr::ReadCommand_F(command, instr));
      break;

    case 'C':
      // Close
      RETURN_NOT_OK(PGInstr::ReadCommand_C(command, instr));
      break;

    case 'D':
      // describe.
      RETURN_NOT_OK(PGInstr::ReadCommand_D(command, instr));
      break;

    case 'H':
      // flush.
      RETURN_NOT_OK(PGInstr::ReadCommand_H(command, instr));
      break;

    case 'S':                       /* sync */
      RETURN_NOT_OK(PGInstr::ReadCommand_S(command, instr));
      break;

    case 'X':
      /*
       * 'X' means that the frontend is closing down the socket. EOF
       * means unexpected loss of frontend connection. Either way,
       * perform normal shutdown.
       */
      RETURN_NOT_OK(PGInstr::ReadCommand_X(command, instr));
      break;

    case EOF:
      RETURN_NOT_OK(PGInstr::ReadCommand_EOF(command, instr));
      break;

    case 'd':
      RETURN_NOT_OK(PGInstr::ReadCommand_d(command, instr));
      break;

    case 'c':
      RETURN_NOT_OK(PGInstr::ReadCommand_c(command, instr));
      break;

    case 'f':
      RETURN_NOT_OK(PGInstr::ReadCommand_f(command, instr));
      break;

    default:
      return STATUS(NetworkError,
                    Substitute("invalid frontend message type $0", command->content_type));
  }

  return Status::OK();
}

}  // namespace pgapi
}  // namespace yb
