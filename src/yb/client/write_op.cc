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

#include "yb/client/write_op.h"

#include "yb/client/client.h"
#include "yb/common/encoded_key.h"
#include "yb/common/row.h"
#include "yb/common/wire_protocol.pb.h"

namespace yb {
namespace client {

using sp::shared_ptr;

RowOperationsPB_Type ToInternalWriteType(YBWriteOperation::Type type) {
  switch (type) {
    case YBWriteOperation::INSERT: return RowOperationsPB_Type_INSERT;
    case YBWriteOperation::UPDATE: return RowOperationsPB_Type_UPDATE;
    case YBWriteOperation::DELETE: return RowOperationsPB_Type_DELETE;
    default: LOG(FATAL) << "Unexpected write operation type: " << type;
  }
}

// WriteOperation --------------------------------------------------------------

YBWriteOperation::YBWriteOperation(const shared_ptr<YBTable>& table)
  : table_(table),
    row_(table->schema().schema_) {
}

YBWriteOperation::~YBWriteOperation() {}

int64_t YBWriteOperation::SizeInBuffer() const {
  const Schema* schema = row_.schema();
  int size = 1; // for the operation type

  // Add size of isset bitmap (always present).
  size += BitmapSize(schema->num_columns());
  // Add size of null bitmap (present if the schema has nullables)
  size += ContiguousRowHelper::null_bitmap_size(*schema);
  // The column data itself:
  for (int i = 0; i < schema->num_columns(); i++) {
    if (row_.IsColumnSet(i) && !row_.IsNull(i)) {
      size += schema->column(i).type_info()->size();
      if (schema->column(i).type_info()->physical_type() == BINARY) {
        ContiguousRow row(schema, row_.row_data_);
        Slice bin;
        memcpy(&bin, row.cell_ptr(i), sizeof(bin));
        size += bin.size();
      }
    }
  }
  return size;
}

// Insert -----------------------------------------------------------------------

YBInsert::YBInsert(const shared_ptr<YBTable>& table)
  : YBWriteOperation(table) {
}

YBInsert::~YBInsert() {}

// RedisWriteOp -----------------------------------------------------------------

RedisWriteOp::RedisWriteOp(const shared_ptr<YBTable>& table)
    : YBWriteOperation(table) {
}

RedisWriteOp::~RedisWriteOp() {}

// Update -----------------------------------------------------------------------

YBUpdate::YBUpdate(const shared_ptr<YBTable>& table)
  : YBWriteOperation(table) {
}

YBUpdate::~YBUpdate() {}

// Delete -----------------------------------------------------------------------

YBDelete::YBDelete(const shared_ptr<YBTable>& table)
  : YBWriteOperation(table) {
}

YBDelete::~YBDelete() {}

} // namespace client
} // namespace yb
