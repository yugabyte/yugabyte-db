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

#include "kudu/common/row_operations.h"

#include "kudu/common/partial_row.h"
#include "kudu/common/row_changelist.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/bitmap.h"
#include "kudu/util/faststring.h"
#include "kudu/util/safe_math.h"
#include "kudu/util/slice.h"

using std::string;
using strings::Substitute;

namespace kudu {

string DecodedRowOperation::ToString(const Schema& schema) const {
  switch (type) {
    case RowOperationsPB::INSERT:
      return "INSERT " + schema.DebugRow(ConstContiguousRow(&schema, row_data));
    case RowOperationsPB::UPDATE:
    case RowOperationsPB::DELETE:
      return Substitute("MUTATE $0 $1",
                        schema.DebugRowKey(ConstContiguousRow(&schema, row_data)),
                        changelist.ToString(schema));
    case RowOperationsPB::SPLIT_ROW:
      return Substitute("SPLIT_ROW $0", split_row->ToString());
    default:
      LOG(DFATAL) << "Bad type: " << type;
      return "<bad row operation>";
  }
}

RowOperationsPBEncoder::RowOperationsPBEncoder(RowOperationsPB* pb)
  : pb_(pb) {
}

RowOperationsPBEncoder::~RowOperationsPBEncoder() {
}

void RowOperationsPBEncoder::Add(RowOperationsPB::Type op_type, const KuduPartialRow& partial_row) {
  const Schema* schema = partial_row.schema();

  // See wire_protocol.pb for a description of the format.
  string* dst = pb_->mutable_rows();

  // Compute a bound on much space we may need in the 'rows' field.
  // Then, resize it to this much space. This allows us to use simple
  // memcpy() calls to copy the data, rather than string->append(), which
  // reduces branches significantly in this fairly hot code path.
  // (std::string::append doesn't get inlined).
  // At the end of the function, we'll resize() the string back down to the
  // right size.
  int isset_bitmap_size = BitmapSize(schema->num_columns());
  int null_bitmap_size = ContiguousRowHelper::null_bitmap_size(*schema);
  int type_size = 1; // type uses one byte
  int max_size = type_size + schema->byte_size() + isset_bitmap_size + null_bitmap_size;
  int old_size = dst->size();
  dst->resize(dst->size() + max_size);

  uint8_t* dst_ptr = reinterpret_cast<uint8_t*>(&(*dst)[old_size]);

  *dst_ptr++ = static_cast<uint8_t>(op_type);
  memcpy(dst_ptr, partial_row.isset_bitmap_, isset_bitmap_size);
  dst_ptr += isset_bitmap_size;

  memcpy(dst_ptr,
         ContiguousRowHelper::null_bitmap_ptr(*schema, partial_row.row_data_),
         null_bitmap_size);
  dst_ptr += null_bitmap_size;

  ContiguousRow row(schema, partial_row.row_data_);
  for (int i = 0; i < schema->num_columns(); i++) {
    if (!partial_row.IsColumnSet(i)) continue;
    const ColumnSchema& col = schema->column(i);

    if (col.is_nullable() && row.is_null(i)) continue;

    if (col.type_info()->physical_type() == BINARY) {
      const Slice* val = reinterpret_cast<const Slice*>(row.cell_ptr(i));
      size_t indirect_offset = pb_->mutable_indirect_data()->size();
      pb_->mutable_indirect_data()->append(reinterpret_cast<const char*>(val->data()),
                                           val->size());
      Slice to_append(reinterpret_cast<const uint8_t*>(indirect_offset),
                      val->size());
      memcpy(dst_ptr, &to_append, sizeof(Slice));
      dst_ptr += sizeof(Slice);
    } else {
      memcpy(dst_ptr, row.cell_ptr(i), col.type_info()->size());
      dst_ptr += col.type_info()->size();
    }
  }

  dst->resize(reinterpret_cast<char*>(dst_ptr) - &(*dst)[0]);
}

// ------------------------------------------------------------
// Decoder
// ------------------------------------------------------------

RowOperationsPBDecoder::RowOperationsPBDecoder(const RowOperationsPB* pb,
                                               const Schema* client_schema,
                                               const Schema* tablet_schema,
                                               Arena* dst_arena)
  : pb_(pb),
    client_schema_(client_schema),
    tablet_schema_(tablet_schema),
    dst_arena_(dst_arena),
    bm_size_(BitmapSize(client_schema_->num_columns())),
    tablet_row_size_(ContiguousRowHelper::row_size(*tablet_schema_)),
    src_(pb->rows().data(), pb->rows().size()) {
}

RowOperationsPBDecoder::~RowOperationsPBDecoder() {
}

Status RowOperationsPBDecoder::ReadOpType(RowOperationsPB::Type* type) {
  if (PREDICT_FALSE(src_.empty())) {
    return Status::Corruption("Cannot find operation type");
  }
  if (PREDICT_FALSE(!RowOperationsPB_Type_IsValid(src_[0]))) {
    return Status::Corruption(Substitute("Unknown operation type: $0", src_[0]));
  }
  *type = static_cast<RowOperationsPB::Type>(src_[0]);
  src_.remove_prefix(1);
  return Status::OK();
}

Status RowOperationsPBDecoder::ReadIssetBitmap(const uint8_t** bitmap) {
  if (PREDICT_FALSE(src_.size() < bm_size_)) {
    *bitmap = nullptr;
    return Status::Corruption("Cannot find isset bitmap");
  }
  *bitmap = src_.data();
  src_.remove_prefix(bm_size_);
  return Status::OK();
}

Status RowOperationsPBDecoder::ReadNullBitmap(const uint8_t** null_bm) {
  if (PREDICT_FALSE(src_.size() < bm_size_)) {
    *null_bm = nullptr;
    return Status::Corruption("Cannot find null bitmap");
  }
  *null_bm = src_.data();
  src_.remove_prefix(bm_size_);
  return Status::OK();
}

Status RowOperationsPBDecoder::GetColumnSlice(const ColumnSchema& col, Slice* slice) {
  int size = col.type_info()->size();
  if (PREDICT_FALSE(src_.size() < size)) {
    return Status::Corruption("Not enough data for column", col.ToString());
  }
  // Find the data
  if (col.type_info()->physical_type() == BINARY) {
    // The Slice in the protobuf has a pointer relative to the indirect data,
    // not a real pointer. Need to fix that.
    const Slice* ptr_slice = reinterpret_cast<const Slice*>(src_.data());
    size_t offset_in_indirect = reinterpret_cast<uintptr_t>(ptr_slice->data());
    bool overflowed = false;
    size_t max_offset = AddWithOverflowCheck(offset_in_indirect, ptr_slice->size(), &overflowed);
    if (PREDICT_FALSE(overflowed || max_offset > pb_->indirect_data().size())) {
      return Status::Corruption("Bad indirect slice");
    }

    *slice = Slice(&pb_->indirect_data()[offset_in_indirect], ptr_slice->size());
  } else {
    *slice = Slice(src_.data(), size);
  }
  src_.remove_prefix(size);
  return Status::OK();
}

Status RowOperationsPBDecoder::ReadColumn(const ColumnSchema& col, uint8_t* dst) {
  Slice slice;
  RETURN_NOT_OK(GetColumnSlice(col, &slice));
  if (col.type_info()->physical_type() == BINARY) {
    memcpy(dst, &slice, col.type_info()->size());
  } else {
    slice.relocate(dst);
  }
  return Status::OK();
}

bool RowOperationsPBDecoder::HasNext() const {
  return !src_.empty();
}

namespace {

void SetupPrototypeRow(const Schema& schema,
                       ContiguousRow* row) {
  for (int i = 0; i < schema.num_columns(); i++) {
    const ColumnSchema& col = schema.column(i);
    if (col.has_write_default()) {
      if (col.is_nullable()) {
        row->set_null(i, false);
      }
      memcpy(row->mutable_cell_ptr(i), col.write_default_value(), col.type_info()->size());
    } else if (col.is_nullable()) {
      row->set_null(i, true);
    } else {
      // No default and not nullable. Therefore this column is required,
      // and we'll ensure that it gets during the projection step.
    }
  }
}
} // anonymous namespace

// Projector implementation which handles mapping the client column indexes
// to server-side column indexes, ensuring that all of the columns exist,
// and that every required (non-null, non-default) column in the server
// schema is also present in the client.
class ClientServerMapping {
 public:
  ClientServerMapping(const Schema* client_schema,
                      const Schema* tablet_schema)
    : client_schema_(client_schema),
      tablet_schema_(tablet_schema),
      saw_tablet_col_(tablet_schema->num_columns()) {
  }

  Status ProjectBaseColumn(size_t client_col_idx, size_t tablet_col_idx) {
    // We should get this called exactly once for every input column,
    // since the input columns must be a strict subset of the tablet columns.
    DCHECK_EQ(client_to_tablet_.size(), client_col_idx);
    DCHECK_LT(tablet_col_idx, saw_tablet_col_.size());
    client_to_tablet_.push_back(tablet_col_idx);
    saw_tablet_col_[tablet_col_idx] = 1;
    return Status::OK();
  }

  Status ProjectDefaultColumn(size_t client_col_idx) {
    // Even if the client provides a default (which it shouldn't), we don't
    // want to accept writes with an extra column.
    return ProjectExtraColumn(client_col_idx);
  }

  Status ProjectExtraColumn(size_t client_col_idx) {
    return Status::InvalidArgument(
      Substitute("Client provided column $0 not present in tablet",
                 client_schema_->column(client_col_idx).ToString()));
  }

  // Translate from a client schema index to the tablet schema index
  int client_to_tablet_idx(int client_idx) const {
    DCHECK_LT(client_idx, client_to_tablet_.size());
    return client_to_tablet_[client_idx];
  }

  int num_mapped() const {
    return client_to_tablet_.size();
  }

  // Ensure that any required (non-null, non-defaulted) columns from the
  // server side schema are found in the client-side schema. If not,
  // returns an InvalidArgument.
  Status CheckAllRequiredColumnsPresent() {
    for (int tablet_col_idx = 0;
         tablet_col_idx < tablet_schema_->num_columns();
         tablet_col_idx++) {
      const ColumnSchema& col = tablet_schema_->column(tablet_col_idx);
      if (!col.has_write_default() &&
          !col.is_nullable()) {
        // All clients must pass this column.
        if (!saw_tablet_col_[tablet_col_idx]) {
          return Status::InvalidArgument(
            "Client missing required column", col.ToString());
        }
      }
    }
    return Status::OK();
  }

 private:
  const Schema* const client_schema_;
  const Schema* const tablet_schema_;
  vector<int> client_to_tablet_;
  vector<bool> saw_tablet_col_;
  DISALLOW_COPY_AND_ASSIGN(ClientServerMapping);
};


Status RowOperationsPBDecoder::DecodeInsert(const uint8_t* prototype_row_storage,
                                            const ClientServerMapping& mapping,
                                            DecodedRowOperation* op) {
  const uint8_t* client_isset_map;
  const uint8_t* client_null_map;

  // Read the null and isset bitmaps for the client-provided row.
  RETURN_NOT_OK(ReadIssetBitmap(&client_isset_map));
  if (client_schema_->has_nullables()) {
    RETURN_NOT_OK(ReadNullBitmap(&client_null_map));
  }

  // Allocate a row with the tablet's layout.
  uint8_t* tablet_row_storage = reinterpret_cast<uint8_t*>(
    dst_arena_->AllocateBytesAligned(tablet_row_size_, 8));
  if (PREDICT_FALSE(!tablet_row_storage)) {
    return Status::RuntimeError("Out of memory");
  }

  // Initialize the new row from the 'prototype' row which has been set
  // with all of the server-side default values. This copy may be entirely
  // overwritten in the case that all columns are specified, but this is
  // still likely faster (and simpler) than looping through all the server-side
  // columns to initialize defaults where non-set on every row.
  memcpy(tablet_row_storage, prototype_row_storage, tablet_row_size_);
  ContiguousRow tablet_row(tablet_schema_, tablet_row_storage);

  // Now handle each of the columns passed by the user, replacing the defaults
  // from the prototype.
  for (int client_col_idx = 0; client_col_idx < client_schema_->num_columns(); client_col_idx++) {
    // Look up the corresponding column from the tablet. We use the server-side
    // ColumnSchema object since it has the most up-to-date default, nullability,
    // etc.
    int tablet_col_idx = mapping.client_to_tablet_idx(client_col_idx);
    DCHECK_GE(tablet_col_idx, 0);
    const ColumnSchema& col = tablet_schema_->column(tablet_col_idx);

    if (BitmapTest(client_isset_map, client_col_idx)) {
      // If the client provided a value for this column, copy it.

      // Copy null-ness, if the server side column is nullable.
      bool client_set_to_null = col.is_nullable() &&
        BitmapTest(client_null_map, client_col_idx);
      if (col.is_nullable()) {
        tablet_row.set_null(tablet_col_idx, client_set_to_null);
      }
      // Copy the value if it's not null
      if (!client_set_to_null) {
        RETURN_NOT_OK(ReadColumn(col, tablet_row.mutable_cell_ptr(tablet_col_idx)));
      }
    } else {
      // If the client didn't provide a value, then the column must either be nullable or
      // have a default (which was already set in the prototype row.

      if (PREDICT_FALSE(!(col.is_nullable() || col.has_write_default()))) {
        // TODO: change this to return per-row errors. Otherwise if one row in a batch
        // is missing a field for some reason, the whole batch will fail.
        return Status::InvalidArgument("No value provided for required column",
                                       col.ToString());
      }
    }
  }

  op->row_data = tablet_row_storage;
  return Status::OK();
}

Status RowOperationsPBDecoder::DecodeUpdateOrDelete(const ClientServerMapping& mapping,
                                                    DecodedRowOperation* op) {
  int rowkey_size = tablet_schema_->key_byte_size();

  const uint8_t* client_isset_map;
  const uint8_t* client_null_map;

  // Read the null and isset bitmaps for the client-provided row.
  RETURN_NOT_OK(ReadIssetBitmap(&client_isset_map));
  if (client_schema_->has_nullables()) {
    RETURN_NOT_OK(ReadNullBitmap(&client_null_map));
  }

  // Allocate space for the row key.
  uint8_t* rowkey_storage = reinterpret_cast<uint8_t*>(
    dst_arena_->AllocateBytesAligned(rowkey_size, 8));
  if (PREDICT_FALSE(!rowkey_storage)) {
    return Status::RuntimeError("Out of memory");
  }

  // We're passing the full schema instead of the key schema here.
  // That's OK because the keys come at the bottom. We lose some bounds
  // checking in debug builds, but it avoids an extra copy of the key schema.
  ContiguousRow rowkey(tablet_schema_, rowkey_storage);

  // First process the key columns.
  int client_col_idx = 0;
  for (; client_col_idx < client_schema_->num_key_columns(); client_col_idx++) {
    // Look up the corresponding column from the tablet. We use the server-side
    // ColumnSchema object since it has the most up-to-date default, nullability,
    // etc.
    DCHECK_EQ(mapping.client_to_tablet_idx(client_col_idx),
              client_col_idx) << "key columns should match";
    int tablet_col_idx = client_col_idx;

    const ColumnSchema& col = tablet_schema_->column(tablet_col_idx);
    if (PREDICT_FALSE(!BitmapTest(client_isset_map, client_col_idx))) {
      return Status::InvalidArgument("No value provided for key column",
                                     col.ToString());
    }

    bool client_set_to_null = client_schema_->has_nullables() &&
      BitmapTest(client_null_map, client_col_idx);
    if (PREDICT_FALSE(client_set_to_null)) {
      return Status::InvalidArgument("NULL values not allowed for key column",
                                     col.ToString());
    }

    RETURN_NOT_OK(ReadColumn(col, rowkey.mutable_cell_ptr(tablet_col_idx)));
  }
  op->row_data = rowkey_storage;

  // Now we process the rest of the columns:
  // For UPDATE, we expect at least one other column to be set, indicating the
  // update to perform.
  // For DELETE, we expect no other columns to be set (and we verify that).
  if (op->type == RowOperationsPB::UPDATE) {
    faststring buf;
    RowChangeListEncoder rcl_encoder(&buf);

    // Now process the rest of columns as updates.
    for (; client_col_idx < client_schema_->num_columns(); client_col_idx++) {
      int tablet_col_idx = mapping.client_to_tablet_idx(client_col_idx);
      DCHECK_GE(tablet_col_idx, 0);
      const ColumnSchema& col = tablet_schema_->column(tablet_col_idx);

      if (BitmapTest(client_isset_map, client_col_idx)) {
        bool client_set_to_null = client_schema_->has_nullables() &&
          BitmapTest(client_null_map, client_col_idx);
        uint8_t scratch[kLargestTypeSize];
        uint8_t* val_to_add;
        if (!client_set_to_null) {
          RETURN_NOT_OK(ReadColumn(col, scratch));
          val_to_add = scratch;
        } else {

          if (PREDICT_FALSE(!col.is_nullable())) {
            return Status::InvalidArgument("NULL value not allowed for non-nullable column",
                                           col.ToString());
          }
          val_to_add = nullptr;
        }
        rcl_encoder.AddColumnUpdate(col, tablet_schema_->column_id(tablet_col_idx), val_to_add);
      }
    }

    if (PREDICT_FALSE(buf.size() == 0)) {
      // No actual column updates specified!
      return Status::InvalidArgument("No fields updated, key is",
                                     tablet_schema_->DebugRowKey(rowkey));
    }

    // Copy the row-changelist to the arena.
    uint8_t* rcl_in_arena = reinterpret_cast<uint8_t*>(
      dst_arena_->AllocateBytesAligned(buf.size(), 8));
    if (PREDICT_FALSE(rcl_in_arena == nullptr)) {
      return Status::RuntimeError("Out of memory allocating RCL");
    }
    memcpy(rcl_in_arena, buf.data(), buf.size());
    op->changelist = RowChangeList(Slice(rcl_in_arena, buf.size()));
  } else if (op->type == RowOperationsPB::DELETE) {

    // Ensure that no other columns are set.
    for (; client_col_idx < client_schema_->num_columns(); client_col_idx++) {
      if (BitmapTest(client_isset_map, client_col_idx)) {
        int tablet_col_idx = mapping.client_to_tablet_idx(client_col_idx);
        DCHECK_GE(tablet_col_idx, 0);
        const ColumnSchema& col = tablet_schema_->column(tablet_col_idx);

        return Status::InvalidArgument("DELETE should not have a value for column",
                                       col.ToString());
      }
    }
    op->changelist = RowChangeList::CreateDelete();
  } else {
    LOG(FATAL) << "Should only call this method with UPDATE or DELETE";
  }

  return Status::OK();
}

Status RowOperationsPBDecoder::DecodeSplitRow(const ClientServerMapping& mapping,
                                              DecodedRowOperation* op) {
  op->split_row.reset(new KuduPartialRow(tablet_schema_));

  const uint8_t* client_isset_map;
  const uint8_t* client_null_map;

  // Read the null and isset bitmaps for the client-provided row.
  RETURN_NOT_OK(ReadIssetBitmap(&client_isset_map));
  if (client_schema_->has_nullables()) {
    RETURN_NOT_OK(ReadNullBitmap(&client_null_map));
  }

  // Now handle each of the columns passed by the user.
  for (int client_col_idx = 0; client_col_idx < client_schema_->num_columns(); client_col_idx++) {
    // Look up the corresponding column from the tablet. We use the server-side
    // ColumnSchema object since it has the most up-to-date default, nullability,
    // etc.
    int tablet_col_idx = mapping.client_to_tablet_idx(client_col_idx);
    DCHECK_GE(tablet_col_idx, 0);
    const ColumnSchema& col = tablet_schema_->column(tablet_col_idx);

    if (BitmapTest(client_isset_map, client_col_idx)) {
      // If the client provided a value for this column, copy it.
      Slice column_slice;
      RETURN_NOT_OK(GetColumnSlice(col, &column_slice));
      const uint8_t* data;
      if (col.type_info()->physical_type() == BINARY) {
        data = reinterpret_cast<const uint8_t*>(&column_slice);
      } else {
        data = column_slice.data();
      }
      RETURN_NOT_OK(op->split_row->Set(tablet_col_idx, data));
    }
  }
  return Status::OK();
}

Status RowOperationsPBDecoder::DecodeOperations(vector<DecodedRowOperation>* ops) {
  // TODO: there's a bug here, in that if a client passes some column
  // in its schema that has been deleted on the server, it will fail
  // even if the client never actually specified any values for it.
  // For example, a DBA might do a thorough audit that no one is using
  // some column anymore, and then drop the column, expecting it to be
  // compatible, but all writes would start failing until clients
  // refreshed their schema.
  // See DISABLED_TestProjectUpdatesSubsetOfColumns
  CHECK(!client_schema_->has_column_ids());
  DCHECK(tablet_schema_->has_column_ids());
  ClientServerMapping mapping(client_schema_, tablet_schema_);
  RETURN_NOT_OK(client_schema_->GetProjectionMapping(*tablet_schema_, &mapping));
  DCHECK_EQ(mapping.num_mapped(), client_schema_->num_columns());
  RETURN_NOT_OK(mapping.CheckAllRequiredColumnsPresent());

  // Make a "prototype row" which has all the defaults filled in. We can copy
  // this to create a starting point for each row as we decode it, with
  // all the defaults in place without having to loop.
  uint8_t prototype_row_storage[tablet_row_size_];
  ContiguousRow prototype_row(tablet_schema_, prototype_row_storage);
  SetupPrototypeRow(*tablet_schema_, &prototype_row);

  while (HasNext()) {
    RowOperationsPB::Type type;
    RETURN_NOT_OK(ReadOpType(&type));
    DecodedRowOperation op;
    op.type = type;

    switch (type) {
      case RowOperationsPB::UNKNOWN:
        return Status::NotSupported("Unknown row operation type");
      case RowOperationsPB::INSERT:
        RETURN_NOT_OK(DecodeInsert(prototype_row_storage, mapping, &op));
        break;
      case RowOperationsPB::UPDATE:
      case RowOperationsPB::DELETE:
        RETURN_NOT_OK(DecodeUpdateOrDelete(mapping, &op));
        break;
      case RowOperationsPB::SPLIT_ROW:
        RETURN_NOT_OK(DecodeSplitRow(mapping, &op));
        break;
    }

    ops->push_back(op);
  }
  return Status::OK();
}

} // namespace kudu
