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

#include "yb/yql/pgsql/ybpostgres/pg_send.h"

#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"
#include "yb/yql/pgsql/ybpostgres/pg_type.h"
#include "yb/common/ql_value.h"
#include "yb/util/bytes_formatter.h"

namespace yb {
namespace pgapi {

using strings::Substitute;

//--------------------------------------------------------------------------------------------------
// Constructor.
PGSend::PGSend() {
}

//--------------------------------------------------------------------------------------------------

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
    appendBinaryStringInfo(command, reinterpret_cast<char *>(&n32), sizeof(n32));
  }

  // Write content.
  appendBinaryStringInfo(command, packet->buffer(), packet->size());

  // Write the status.
  if (server_status != '\0') {
    StringInfo zmsg;
    WriteRpcStatus(server_status, &zmsg);
    command->data.append(zmsg->data);
  }

  *pg_command = command;
}

void PGSend::WriteRpcStatus(char server_status, StringInfo *zmsg) const {
  StringInfo zpacket = pgapi::makeStringInfo(static_cast<int>('Z'));
  pgapi::appendStringInfoChar(zpacket, server_status);
  WriteRpcCommand(zpacket, 0, zmsg);
}

//--------------------------------------------------------------------------------------------------

void PGSend::WriteAuthRpcCommand(const PGPort& pgport,
                                 string extra_data,
                                 StringInfo *auth_command) const {
  // Accept all clients without any authentication.
  int32_t areq = static_cast<int32_t>(AuthRequest::AUTH_REQ_OK);
  PGPqFormatter formatter;
  formatter.pq_beginmessage('R');
  formatter.pq_sendint(areq, sizeof(areq));
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

//--------------------------------------------------------------------------------------------------

void PGSend::WriteTupleDesc(const PgsqlRSRowDesc& tuple_desc, faststring *rows_data) {
  // 'T': Tuple descriptor message type.
  PGPqFormatter formatter;
  formatter.pq_beginmessage('T');

  // Number of columns in tuples.
  uint16_t ncols = tuple_desc.rscol_count();
  formatter.pq_sendint(ncols, sizeof(ncols));

  // Write the column descriptor.
  int version = PG_PROTOCOL_MAJOR(protocol_version_);
  for (const PgsqlRSRowDesc::RSColDesc& col_desc : tuple_desc.rscol_descs()) {
    // Send column name.
    formatter.pq_sendstring(col_desc.name().c_str());

    // TODO(neil) MUST send correct table ID and column ID.
    if (version >= 3) {
      // Send table ID (4 bytes).
      formatter.pq_sendint(0, 4);
      // Send column ID (2 bytes).
      formatter.pq_sendint(0, 2);
    }

    // Send type ID (4 bytes).
    formatter.pq_sendint(PgTypeId(col_desc.yql_typeid()), 4);

    // Send type length (2 bytes).
    formatter.pq_sendint(PgTypeLength(col_desc.yql_typeid()), 2);

    // Send type modifier (4 bytes).
    // TODO(neil) Must send correct value. For now, send the value for standard type (-1).
    formatter.pq_sendint(-1, 4);

    // Send format (2 bytes).  0 for text, 1 for binary.
    if (version >= 3) {
      formatter.pq_sendint(0, 2);
    }
  }

  StringInfo pg_command;
  WriteRpcCommand(formatter.send_buf(), '\0', &pg_command);
  rows_data->append(pg_command->data);
}

void PGSend::WriteTuples(const PgsqlResultSet& tuples,
                         const PgsqlRSRowDesc& tuple_desc,
                         faststring *rows_data) {
  for (const PgsqlRSRow& tuple : tuples.rsrows()) {
    // Start writing a row.
    PGPqFormatter formatter;
    formatter.pq_beginmessage('D');

    // Send number of columns.
    CHECK_EQ(tuple.rscol_count(), tuple_desc.rscol_count());
    int count = tuple_desc.rscol_count();
    formatter.pq_sendint(count, 2);

    // Send column values.
    int col_index = 0;
    for (const PgsqlRSRowDesc::RSColDesc& col_desc : tuple_desc.rscol_descs()) {
      const QLValue& col_value = tuple.rscol_value(col_index);
      if (col_value.IsNull()) {
        formatter.pq_sendint(-1, 4);
      } else {
        // TODO(neil) We also need to support binary format for efficiency.
        // Output text only for now.
        const string data = WriteColumnToString(col_value, col_desc.ql_type());
        formatter.pq_sendcountedtext(data.c_str(), data.size(), false);
      }
      col_index++;
    }

    // Complete the tuple / row.
    StringInfo pg_command;
    WriteRpcCommand(formatter.send_buf(), '\0', &pg_command);
    rows_data->append(pg_command->data);
  }
}

// TODO(neil) Conversion to string should not need a switch on datatype.
// PostgreSQL use builtin function table to optimize all conversions. Instead of using C function
// pointers, we can use template<type> to call the serialize routines directly without switching.
string PGSend::WriteColumnToString(const QLValue& col_value, const QLType::SharedPtr& col_type) {
  switch (col_type->main()) {
    case DataType::INT16:
      return std::to_string(col_value.int16_value());
    case DataType::INT32:
      return std::to_string(col_value.int32_value());
    case DataType::INT64:
      return std::to_string(col_value.int64_value());
    case DataType::FLOAT:
      return std::to_string(col_value.float_value());
    case DataType::DOUBLE:
      return std::to_string(col_value.double_value());
    case DataType::BOOL:
      return (col_value.bool_value() ? "true" : "false");
    case DataType::STRING:
      return col_value.string_value().c_str();
    case DataType::TIMEUUID:
      return col_value.timeuuid_value().ToString();

    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::TIMESTAMP: FALLTHROUGH_INTENDED;
    case DataType::DECIMAL: FALLTHROUGH_INTENDED;
    case DataType::INET: FALLTHROUGH_INTENDED;
    case DataType::UUID: FALLTHROUGH_INTENDED;
    case DataType::DATE: FALLTHROUGH_INTENDED;
    case DataType::TIME:
      // Should never get here. Semantic checks should already report errors.
      LOG(FATAL) << "DEVELOPERS: Serializing support is not yet implemented";
      break;

    case DataType::UNKNOWN_DATA: FALLTHROUGH_INTENDED;
    case DataType::NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case DataType::INT8: FALLTHROUGH_INTENDED;
    case DataType::VARINT: FALLTHROUGH_INTENDED;
    case DataType::LIST: FALLTHROUGH_INTENDED;
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::FROZEN: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::UINT16: FALLTHROUGH_INTENDED;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::UINT64: FALLTHROUGH_INTENDED;
    case DataType::TYPEARGS: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE:
    default:
      // Should never get here. Semantic checks should already report errors.
      LOG(FATAL) << "Unknown datatype in PostgreSQL";
      return nullptr;
  }
}

}  // namespace pgapi
}  // namespace yb
