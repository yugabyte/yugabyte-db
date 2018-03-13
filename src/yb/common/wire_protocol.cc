// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/common/wire_protocol.h"

#include <string>
#include <vector>

#include "yb/common/row.h"
#include "yb/gutil/port.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/faststring.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/safe_math.h"
#include "yb/util/slice.h"

using google::protobuf::RepeatedPtrField;
using std::vector;

namespace yb {

void StatusToPB(const Status& status, AppStatusPB* pb) {
  pb->Clear();
  bool is_unknown = false;
  if (status.ok()) {
    pb->set_code(AppStatusPB::OK);
    // OK statuses don't have any message or posix code.
    return;
  } else if (status.IsNotFound()) {
    pb->set_code(AppStatusPB::NOT_FOUND);
  } else if (status.IsCorruption()) {
    pb->set_code(AppStatusPB::CORRUPTION);
  } else if (status.IsNotSupported()) {
    pb->set_code(AppStatusPB::NOT_SUPPORTED);
  } else if (status.IsInvalidArgument()) {
    pb->set_code(AppStatusPB::INVALID_ARGUMENT);
  } else if (status.IsIOError()) {
    pb->set_code(AppStatusPB::IO_ERROR);
  } else if (status.IsAlreadyPresent()) {
    pb->set_code(AppStatusPB::ALREADY_PRESENT);
  } else if (status.IsRuntimeError()) {
    pb->set_code(AppStatusPB::RUNTIME_ERROR);
  } else if (status.IsNetworkError()) {
    pb->set_code(AppStatusPB::NETWORK_ERROR);
  } else if (status.IsIllegalState()) {
    pb->set_code(AppStatusPB::ILLEGAL_STATE);
  } else if (status.IsNotAuthorized()) {
    pb->set_code(AppStatusPB::NOT_AUTHORIZED);
  } else if (status.IsAborted()) {
    pb->set_code(AppStatusPB::ABORTED);
  } else if (status.IsRemoteError()) {
    pb->set_code(AppStatusPB::REMOTE_ERROR);
  } else if (status.IsServiceUnavailable()) {
    pb->set_code(AppStatusPB::SERVICE_UNAVAILABLE);
  } else if (status.IsTimedOut()) {
    pb->set_code(AppStatusPB::TIMED_OUT);
  } else if (status.IsUninitialized()) {
    pb->set_code(AppStatusPB::UNINITIALIZED);
  } else if (status.IsConfigurationError()) {
    pb->set_code(AppStatusPB::CONFIGURATION_ERROR);
  } else if (status.IsIncomplete()) {
    pb->set_code(AppStatusPB::INCOMPLETE);
  } else if (status.IsEndOfFile()) {
    pb->set_code(AppStatusPB::END_OF_FILE);
  } else if (status.IsInvalidCommand()) {
    pb->set_code(AppStatusPB::INVALID_COMMAND);
  } else if (status.IsQLError()) {
    pb->set_code(AppStatusPB::SQL_ERROR);
  } else if (status.IsInternalError()) {
    pb->set_code(AppStatusPB::INTERNAL_ERROR);
  } else if (status.IsExpired()) {
    pb->set_code(AppStatusPB::EXPIRED);
  } else if (status.IsLeaderHasNoLease()) {
    pb->set_code(AppStatusPB::LEADER_HAS_NO_LEASE);
  } else if (status.IsLeaderNotReadyToServe()) {
    pb->set_code(AppStatusPB::LEADER_NOT_READY_TO_SERVE);
  } else if (status.IsTryAgain()) {
    pb->set_code(AppStatusPB::TRY_AGAIN_CODE);
  } else if (status.IsBusy()) {
    pb->set_code(AppStatusPB::BUSY);
  } else {
    LOG(WARNING) << "Unknown error code translation from internal error "
                 << status.ToString() << ": sending UNKNOWN_ERROR";
    pb->set_code(AppStatusPB::UNKNOWN_ERROR);
    is_unknown = true;
  }
  if (is_unknown) {
    // For unknown status codes, include the original stringified error
    // code.
    pb->set_message(status.CodeAsString() + ": " + status.message().ToBuffer());
  } else {
    // Otherwise, just encode the message itself, since the other end
    // will reconstruct the other parts of the ToString() response.
    pb->set_message(status.message().cdata(), status.message().size());
  }
  if (status.IsQLError()) {
    pb->set_ql_error_code(status.error_code());
  } else if (status.error_code() != -1) {
    pb->set_posix_code(status.error_code());
  }
}

Status StatusFromPB(const AppStatusPB& pb) {
  int posix_code = pb.has_posix_code() ? pb.posix_code() : -1;

  switch (pb.code()) {
    case AppStatusPB::OK:
      return Status::OK();
    case AppStatusPB::NOT_FOUND:
      return STATUS(NotFound, pb.message(), "", posix_code);
    case AppStatusPB::CORRUPTION:
      return STATUS(Corruption, pb.message(), "", posix_code);
    case AppStatusPB::NOT_SUPPORTED:
      return STATUS(NotSupported, pb.message(), "", posix_code);
    case AppStatusPB::INVALID_ARGUMENT:
      return STATUS(InvalidArgument, pb.message(), "", posix_code);
    case AppStatusPB::IO_ERROR:
      return STATUS(IOError, pb.message(), "", posix_code);
    case AppStatusPB::ALREADY_PRESENT:
      return STATUS(AlreadyPresent, pb.message(), "", posix_code);
    case AppStatusPB::RUNTIME_ERROR:
      return STATUS(RuntimeError, pb.message(), "", posix_code);
    case AppStatusPB::NETWORK_ERROR:
      return STATUS(NetworkError, pb.message(), "", posix_code);
    case AppStatusPB::ILLEGAL_STATE:
      return STATUS(IllegalState, pb.message(), "", posix_code);
    case AppStatusPB::NOT_AUTHORIZED:
      return STATUS(NotAuthorized, pb.message(), "", posix_code);
    case AppStatusPB::ABORTED:
      return STATUS(Aborted, pb.message(), "", posix_code);
    case AppStatusPB::REMOTE_ERROR:
      return STATUS(RemoteError, pb.message(), "", posix_code);
    case AppStatusPB::SERVICE_UNAVAILABLE:
      return STATUS(ServiceUnavailable, pb.message(), "", posix_code);
    case AppStatusPB::TIMED_OUT:
      return STATUS(TimedOut, pb.message(), "", posix_code);
    case AppStatusPB::UNINITIALIZED:
      return STATUS(Uninitialized, pb.message(), "", posix_code);
    case AppStatusPB::CONFIGURATION_ERROR:
      return STATUS(ConfigurationError, pb.message(), "", posix_code);
    case AppStatusPB::INCOMPLETE:
      return STATUS(Incomplete, pb.message(), "", posix_code);
    case AppStatusPB::END_OF_FILE:
      return STATUS(EndOfFile, pb.message(), "", posix_code);
    case AppStatusPB::INVALID_COMMAND:
      return STATUS(InvalidCommand, pb.message(), "", posix_code);
    case AppStatusPB::SQL_ERROR:
      if (!pb.has_ql_error_code()) {
        return STATUS(InternalError, "SQL error code missing");
      }
      return STATUS(QLError, pb.message(), "", pb.ql_error_code());
    case AppStatusPB::INTERNAL_ERROR:
      return STATUS(InternalError, pb.message(), "", posix_code);
    case AppStatusPB::EXPIRED:
      return STATUS(Expired, pb.message(), "", posix_code);
    case AppStatusPB::LEADER_HAS_NO_LEASE:
      return STATUS(LeaderHasNoLease, pb.message(), "", posix_code);
    case AppStatusPB::LEADER_NOT_READY_TO_SERVE:
      return STATUS(LeaderNotReadyToServe, pb.message(), "", posix_code);
    case AppStatusPB::TRY_AGAIN_CODE:
      return STATUS(TryAgain, pb.message(), "", posix_code);
    case AppStatusPB::BUSY:
      return STATUS(Busy, pb.message(), "", posix_code);
    case AppStatusPB::UNKNOWN_ERROR:
    default:
      LOG(WARNING) << "Unknown error code in status: " << pb.ShortDebugString();
      return STATUS_FORMAT(
          RuntimeError, "($0 unknown): $1, $2", pb.code(), pb.message(), posix_code);
  }
}

Status HostPortToPB(const HostPort& host_port, HostPortPB* host_port_pb) {
  host_port_pb->set_host(host_port.host());
  host_port_pb->set_port(host_port.port());
  return Status::OK();
}

Status HostPortFromPB(const HostPortPB& host_port_pb, HostPort* host_port) {
  host_port->set_host(host_port_pb.host());
  host_port->set_port(host_port_pb.port());
  return Status::OK();
}

Status EndpointFromHostPortPB(const HostPortPB& host_portpb, Endpoint* endpoint) {
  HostPort host_port;
  RETURN_NOT_OK(HostPortFromPB(host_portpb, &host_port));
  return EndpointFromHostPort(host_port, endpoint);
}

Status HostPortsToPBs(const std::vector<HostPort>& addrs,
                      RepeatedPtrField<HostPortPB>* pbs) {
  for (const auto& addr : addrs) {
    RETURN_NOT_OK(HostPortToPB(addr, pbs->Add()));
  }
  return Status::OK();
}

Status AddHostPortPBs(const std::vector<Endpoint>& addrs,
                      RepeatedPtrField<HostPortPB>* pbs) {
  for (const auto& addr : addrs) {
    HostPortPB* pb = pbs->Add();
    pb->set_port(addr.port());
    if (addr.address().is_unspecified()) {
      auto status = GetFQDN(pb->mutable_host());
      if (!status.ok()) {
        std::vector<IpAddress> locals;
        if (!GetLocalAddresses(&locals, AddressFilter::EXTERNAL).ok() || locals.empty()) {
          return status;
        }
        for (auto& address : locals) {
          if (pb == nullptr) {
            pb = pbs->Add();
            pb->set_port(addr.port());
          }
          pb->set_host(address.to_string());
          pb = nullptr;
        }
      }
    } else {
      pb->set_host(addr.address().to_string());
    }
  }
  return Status::OK();
}

Status SchemaToPB(const Schema& schema, SchemaPB *pb, int flags) {
  pb->Clear();
  RETURN_NOT_OK(SchemaToColumnPBs(schema, pb->mutable_columns(), flags));
  schema.table_properties().ToTablePropertiesPB(pb->mutable_table_properties());
  return Status::OK();
}

Status SchemaToPBWithoutIds(const Schema& schema, SchemaPB *pb) {
  pb->Clear();
  return SchemaToColumnPBs(schema, pb->mutable_columns(), SCHEMA_PB_WITHOUT_IDS);
}

Status SchemaFromPB(const SchemaPB& pb, Schema *schema) {
  // Conver the columns.
  vector<ColumnSchema> columns;
  vector<ColumnId> column_ids;
  int num_key_columns = 0;
  RETURN_NOT_OK(ColumnPBsToColumnTuple(pb.columns(), &columns, &column_ids, &num_key_columns));

  // Convert the table properties.
  TableProperties table_properties = TableProperties::FromTablePropertiesPB(pb.table_properties());
  return schema->Reset(columns, column_ids, num_key_columns, table_properties);
}

void ColumnSchemaToPB(const ColumnSchema& col_schema, ColumnSchemaPB *pb, int flags) {
  pb->Clear();
  pb->set_name(col_schema.name());
  col_schema.type()->ToQLTypePB(pb->mutable_type());
  pb->set_is_nullable(col_schema.is_nullable());
  pb->set_is_static(col_schema.is_static());
  pb->set_is_counter(col_schema.is_counter());
  pb->set_sorting_type(col_schema.sorting_type());
  // We only need to process the *hash* primary key here. The regular primary key is set by the
  // conversion for SchemaPB. The reason is that ColumnSchema and ColumnSchemaPB are not matching
  // 1 to 1 as ColumnSchema doesn't have "is_key" field. That was Kudu's code, and we keep it that
  // way for now.
  if (col_schema.is_hash_key()) {
    pb->set_is_key(true);
    pb->set_is_hash_key(true);
  }
}

ColumnSchema ColumnSchemaFromPB(const ColumnSchemaPB& pb) {
  // Only "is_hash_key" is used to construct ColumnSchema. The field "is_key" will be read when
  // processing SchemaPB.
  return ColumnSchema(pb.name(), QLType::FromQLTypePB(pb.type()), pb.is_nullable(),
                      pb.is_hash_key(), pb.is_static(), pb.is_counter(),
                      ColumnSchema::SortingType(pb.sorting_type()));
}

CHECKED_STATUS ColumnPBsToColumnTuple(
    const RepeatedPtrField<ColumnSchemaPB>& column_pbs,
    vector<ColumnSchema>* columns , vector<ColumnId>* column_ids, int* num_key_columns) {
  columns->reserve(column_pbs.size());
  bool is_handling_key = true;
  for (const ColumnSchemaPB& pb : column_pbs) {
    columns->push_back(ColumnSchemaFromPB(pb));
    if (pb.is_key()) {
      if (!is_handling_key) {
        return STATUS(InvalidArgument,
                      "Got out-of-order key column", pb.ShortDebugString());
      }
      (*num_key_columns)++;
    } else {
      is_handling_key = false;
    }
    if (pb.has_id()) {
      column_ids->push_back(ColumnId(pb.id()));
    }
  }

  DCHECK_LE((*num_key_columns), columns->size());
  return Status::OK();
}

Status ColumnPBsToSchema(const RepeatedPtrField<ColumnSchemaPB>& column_pbs,
                         Schema* schema) {

  vector<ColumnSchema> columns;
  vector<ColumnId> column_ids;
  int num_key_columns = 0;
  RETURN_NOT_OK(ColumnPBsToColumnTuple(column_pbs, &columns, &column_ids, &num_key_columns));

  // TODO(perf): could make the following faster by adding a
  // Reset() variant which actually takes ownership of the column
  // vector.
  return schema->Reset(columns, column_ids, num_key_columns);
}

Status SchemaToColumnPBs(const Schema& schema,
                         RepeatedPtrField<ColumnSchemaPB>* cols,
                         int flags) {
  cols->Clear();
  int idx = 0;
  for (const ColumnSchema& col : schema.columns()) {
    ColumnSchemaPB* col_pb = cols->Add();
    ColumnSchemaToPB(col, col_pb);
    col_pb->set_is_key(idx < schema.num_key_columns());

    if (schema.has_column_ids() && !(flags & SCHEMA_PB_WITHOUT_IDS)) {
      col_pb->set_id(schema.column_id(idx));
    }

    idx++;
  }
  return Status::OK();
}

Status FindLeaderHostPort(const RepeatedPtrField<ServerEntryPB>& entries,
                          HostPort* leader_hostport) {
  for (const ServerEntryPB& entry : entries) {
    if (entry.has_error()) {
      LOG(WARNING) << "Error encountered for server entry " << entry.ShortDebugString()
                   << ": " << StatusFromPB(entry.error()).ToString();
      continue;
    }
    if (!entry.has_role()) {
      return STATUS(IllegalState,
          strings::Substitute("Every server in must have a role, but entry ($0) has no role.",
                              entry.ShortDebugString()));
    }
    if (entry.role() == consensus::RaftPeerPB::LEADER) {
      return HostPortFromPB(entry.registration().rpc_addresses(0), leader_hostport);
    }
  }
  return STATUS(NotFound, "No leader found.");
}

} // namespace yb
