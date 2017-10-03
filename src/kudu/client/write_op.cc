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

#include "kudu/client/write_op.h"

#include "kudu/client/client.h"
#include "kudu/common/encoded_key.h"
#include "kudu/common/row.h"
#include "kudu/common/wire_protocol.pb.h"

namespace kudu {
namespace client {

using sp::shared_ptr;

RowOperationsPB_Type ToInternalWriteType(KuduWriteOperation::Type type) {
  switch (type) {
    case KuduWriteOperation::INSERT: return RowOperationsPB_Type_INSERT;
    case KuduWriteOperation::UPDATE: return RowOperationsPB_Type_UPDATE;
    case KuduWriteOperation::DELETE: return RowOperationsPB_Type_DELETE;
    default: LOG(FATAL) << "Unexpected write operation type: " << type;
  }
}

// WriteOperation --------------------------------------------------------------

KuduWriteOperation::KuduWriteOperation(const shared_ptr<KuduTable>& table)
  : table_(table),
    row_(table->schema().schema_) {
}

KuduWriteOperation::~KuduWriteOperation() {}

EncodedKey* KuduWriteOperation::CreateKey() const {
  CHECK(row_.IsKeySet()) << "key must be set";

  ConstContiguousRow row(row_.schema(), row_.row_data_);
  EncodedKeyBuilder kb(row.schema());
  for (int i = 0; i < row.schema()->num_key_columns(); i++) {
    kb.AddColumnKey(row.cell_ptr(i));
  }
  gscoped_ptr<EncodedKey> key(kb.BuildEncodedKey());
  return key.release();
}

int64_t KuduWriteOperation::SizeInBuffer() const {
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

KuduInsert::KuduInsert(const shared_ptr<KuduTable>& table)
  : KuduWriteOperation(table) {
}

KuduInsert::~KuduInsert() {}

// Update -----------------------------------------------------------------------

KuduUpdate::KuduUpdate(const shared_ptr<KuduTable>& table)
  : KuduWriteOperation(table) {
}

KuduUpdate::~KuduUpdate() {}

// Delete -----------------------------------------------------------------------

KuduDelete::KuduDelete(const shared_ptr<KuduTable>& table)
  : KuduWriteOperation(table) {
}

KuduDelete::~KuduDelete() {}

} // namespace client
} // namespace kudu
