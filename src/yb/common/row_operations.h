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
#ifndef YB_COMMON_ROW_OPERATIONS_H
#define YB_COMMON_ROW_OPERATIONS_H

#include <memory>
#include <string>
#include <vector>

#include "yb/common/row_changelist.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/gutil/macros.h"

#include "yb/util/memory/arena_fwd.h"
#include "yb/util/status.h"

namespace yb {

class YBPartialRow;
class Schema;

class ClientServerMapping;

class RowOperationsPBEncoder {
 public:
  explicit RowOperationsPBEncoder(RowOperationsPB* pb);
  ~RowOperationsPBEncoder();

  // Append this partial row to the protobuf.
  void Add(RowOperationsPB::Type type, const YBPartialRow& row);

 private:
  RowOperationsPB* pb_;

  DISALLOW_COPY_AND_ASSIGN(RowOperationsPBEncoder);
};

struct DecodedRowOperation {
  RowOperationsPB::Type type;

  // For INSERT, the whole projected row.
  // For UPDATE or DELETE, the row key.
  const uint8_t* row_data;

  // For UPDATE and DELETE types, the changelist
  RowChangeList changelist;

  // For SPLIT_ROW, the partial row to split on.
  std::shared_ptr<YBPartialRow> split_row;

  std::string ToString(const Schema& schema) const;
};

class RowOperationsPBDecoder {
 public:
  RowOperationsPBDecoder(const RowOperationsPB* pb,
                         const Schema* client_schema,
                         const Schema* tablet_schema,
                         Arena* dst_arena);
  ~RowOperationsPBDecoder();

  CHECKED_STATUS DecodeOperations(std::vector<DecodedRowOperation>* ops);

 private:
  CHECKED_STATUS ReadOpType(RowOperationsPB::Type* type);
  CHECKED_STATUS ReadIssetBitmap(const uint8_t** bitmap);
  CHECKED_STATUS ReadNullBitmap(const uint8_t** null_bm);
  CHECKED_STATUS GetColumnSlice(const ColumnSchema& col, Slice* slice);
  CHECKED_STATUS ReadColumn(const ColumnSchema& col, uint8_t* dst);
  bool HasNext() const;

  CHECKED_STATUS DecodeInsert(const uint8_t* prototype_row_storage,
                      const ClientServerMapping& mapping,
                      DecodedRowOperation* op);
  //------------------------------------------------------------
  // Serialization/deserialization support
  //------------------------------------------------------------

  // Decode the next encoded operation, which must be UPDATE or DELETE.
  CHECKED_STATUS DecodeUpdateOrDelete(const ClientServerMapping& mapping,
                              DecodedRowOperation* op);

  // Decode the next encoded operation, which must be SPLIT_KEY.
  CHECKED_STATUS DecodeSplitRow(const ClientServerMapping& mapping,
                        DecodedRowOperation* op);

  const RowOperationsPB* const pb_;
  const Schema* const client_schema_;
  const Schema* const tablet_schema_;
  Arena* const dst_arena_;

  const int bm_size_;
  const int tablet_row_size_;
  Slice src_;


  DISALLOW_COPY_AND_ASSIGN(RowOperationsPBDecoder);
};
} // namespace yb
#endif /* YB_COMMON_ROW_OPERATIONS_H */
