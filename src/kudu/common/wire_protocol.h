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
// Helpers for dealing with the protobufs defined in wire_protocol.proto.
#ifndef KUDU_COMMON_WIRE_PROTOCOL_H
#define KUDU_COMMON_WIRE_PROTOCOL_H

#include <vector>

#include "kudu/common/wire_protocol.pb.h"
#include "kudu/util/status.h"

namespace kudu {

class ConstContiguousRow;
class ColumnSchema;
class faststring;
class HostPort;
class RowBlock;
class RowBlockRow;
class RowChangeList;
class Schema;
class Slice;
class Sockaddr;

// Convert the given C++ Status object into the equivalent Protobuf.
void StatusToPB(const Status& status, AppStatusPB* pb);

// Convert the given protobuf into the equivalent C++ Status object.
Status StatusFromPB(const AppStatusPB& pb);

// Convert the specified HostPort to protobuf.
Status HostPortToPB(const HostPort& host_port, HostPortPB* host_port_pb);

// Returns the HostPort created from the specified protobuf.
Status HostPortFromPB(const HostPortPB& host_port_pb, HostPort* host_port);

// Adds addresses in 'addrs' to 'pbs'. If an address is a wildcard
// (e.g., "0.0.0.0"), then the local machine's hostname is used in
// its place.
Status AddHostPortPBs(const std::vector<Sockaddr>& addrs,
                      google::protobuf::RepeatedPtrField<HostPortPB>* pbs);

enum SchemaPBConversionFlags {
  SCHEMA_PB_WITHOUT_IDS = 1 << 0,
  SCHEMA_PB_WITHOUT_STORAGE_ATTRIBUTES = 1 << 1,
};

// Convert the specified schema to protobuf.
// 'flags' is a bitfield of SchemaPBConversionFlags values.
Status SchemaToPB(const Schema& schema, SchemaPB* pb, int flags = 0);

// Convert the specified schema to protobuf without column IDs.
Status SchemaToPBWithoutIds(const Schema& schema, SchemaPB *pb);

// Returns the Schema created from the specified protobuf.
// If the schema is invalid, return a non-OK status.
Status SchemaFromPB(const SchemaPB& pb, Schema *schema);

// Convert the specified column schema to protobuf.
// 'flags' is a bitfield of SchemaPBConversionFlags values.
void ColumnSchemaToPB(const ColumnSchema& schema, ColumnSchemaPB *pb, int flags = 0);

// Return the ColumnSchema created from the specified protobuf.
ColumnSchema ColumnSchemaFromPB(const ColumnSchemaPB& pb);

// Convert the given list of ColumnSchemaPB objects into a Schema object.
//
// Returns InvalidArgument if the provided columns don't make a valid Schema
// (eg if the keys are non-contiguous or nullable).
Status ColumnPBsToSchema(
  const google::protobuf::RepeatedPtrField<ColumnSchemaPB>& column_pbs,
  Schema* schema);

// Extract the columns of the given Schema into protobuf objects.
//
// The 'cols' list is replaced by this method.
// 'flags' is a bitfield of SchemaPBConversionFlags values.
Status SchemaToColumnPBs(
  const Schema& schema,
  google::protobuf::RepeatedPtrField<ColumnSchemaPB>* cols,
  int flags = 0);

// Encode the given row block into the provided protobuf and data buffers.
//
// All data (both direct and indirect) for each selected row in the RowBlock is
// copied into the protobuf and faststrings.
// The original data may be destroyed safely after this returns.
//
// This only converts those rows whose selection vector entry is true.
// If 'client_projection_schema' is not NULL, then only columns specified in
// 'client_projection_schema' will be projected to 'data_buf'.
//
// Requires that block.nrows() > 0
void SerializeRowBlock(const RowBlock& block, RowwiseRowBlockPB* rowblock_pb,
                       const Schema* client_projection_schema,
                       faststring* data_buf, faststring* indirect_data);

// Rewrites the data pointed-to by row data slice 'row_data_slice' by replacing
// relative indirect data pointers with absolute ones in 'indirect_data_slice'.
// At the time of this writing, this rewriting is only done for STRING types.
//
// Returns a bad Status if the provided data is invalid or corrupt.
Status RewriteRowBlockPointers(const Schema& schema, const RowwiseRowBlockPB& rowblock_pb,
                               const Slice& indirect_data_slice, Slice* row_data_slice);

// Extract the rows stored in this protobuf, which must have exactly the
// given Schema. This Schema may be obtained using ColumnPBsToSchema.
//
// Pointers are added to 'rows' for each of the extracted rows. These
// pointers are suitable for constructing ConstContiguousRow objects.
// TODO: would be nice to just return a vector<ConstContiguousRow>, but
// they're not currently copyable, so this can't be done.
//
// Note that the returned rows refer to memory managed by 'rows_data' and
// 'indirect_data'. This is also the reason that 'rows_data' is a non-const pointer
// argument: the internal data is mutated in-place to restore the validity of
// indirect data pointers, which are relative on the wire but must be absolute
// while in-memory.
//
// Returns a bad Status if the provided data is invalid or corrupt.
Status ExtractRowsFromRowBlockPB(const Schema& schema,
                                 const RowwiseRowBlockPB& rowblock_pb,
                                 const Slice& indirect_data,
                                 Slice* rows_data,
                                 std::vector<const uint8_t*>* rows);

// Set 'leader_hostport' to the host/port of the leader server if one
// can be found in 'entries'.
//
// Returns Status::NotFound if no leader is found.
Status FindLeaderHostPort(const google::protobuf::RepeatedPtrField<ServerEntryPB>& entries,
                          HostPort* leader_hostport);

} // namespace kudu
#endif
