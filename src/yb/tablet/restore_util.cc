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
//
#include "yb/tablet/restore_util.h"

#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/doc_read_context.h"

#include "yb/dockv/packed_value.h"
#include "yb/dockv/value_packing_v2.h"

#include "yb/rpc/lightweight_message.h"

#include "yb/tablet/tablet_metadata.h"

#include "yb/util/logging.h"

namespace yb {

namespace {

void AddKeyValue(Slice key, Slice value, docdb::DocWriteBatch* write_batch) {
  auto& pair = write_batch->AddRaw();
  pair.key.assign(key.cdata(), key.size());
  pair.value.assign(value.cdata(), value.size());
}

void AddEntry(
    Slice key, dockv::PackedValueV1 value, DataType data_type, docdb::DocWriteBatch* write_batch) {
  AddKeyValue(key, *value, write_batch);
}

void AddEntry(
    Slice key, dockv::PackedValueV2 value, DataType data_type, docdb::DocWriteBatch* write_batch) {
  auto ql_value = CHECK_RESULT(UnpackQLValue(value, data_type));
  ValueBuffer buffer;
  dockv::AppendEncodedValue(ql_value, &buffer);
  AddKeyValue(key, buffer.AsSlice(), write_batch);
}

int64_t GetValue(const dockv::PrimitiveValue& value, int64_t* type) {
  return value.GetInt64();
}

bool GetValue(const dockv::PrimitiveValue& value, bool* type) {
  return value.GetBoolean();
}

template <class ValueType>
Result<std::optional<ValueType>> GetColumnValuePacked(
    tablet::TableInfo* table_info, Slice packed_value, const std::string& column_name) {
  auto value_slice = packed_value;
  RETURN_NOT_OK(dockv::ValueControlFields::Decode(&value_slice));
  auto packed_row_version = dockv::GetPackedRowVersion(value_slice);
  SCHECK(packed_row_version.has_value(), Corruption, "Packed row expected: $0",
         packed_value.ToDebugHexString());
  value_slice.consume_byte();
  const dockv::SchemaPacking& packing = VERIFY_RESULT(
      table_info->doc_read_context->schema_packing_storage.GetPacking(&value_slice));
  auto column_id = VERIFY_RESULT(table_info->schema().ColumnIdByName(column_name));
  auto index = packing.GetIndex(column_id);
  if (index == dockv::SchemaPacking::kSkippedColumnIdx) {
    return std::nullopt;
  }
  switch (*packed_row_version) {
    case dockv::PackedRowVersion::kV1: {
      dockv::PackedRowDecoderV1 decoder(packing, value_slice.data());
      dockv::Value column_value;
      RETURN_NOT_OK(column_value.Decode(*decoder.FetchValue(index)));
      // Using nullptr cast for overload routing.
      return GetValue(column_value.primitive_value(), static_cast<ValueType*>(nullptr));
    }
    case dockv::PackedRowVersion::kV2: {
      dockv::PackedRowDecoderV2 decoder(packing, value_slice.data());
      auto column_value = VERIFY_RESULT(dockv::UnpackPrimitiveValue(
          decoder.FetchValue(index), packing.column_packing_data(index).data_type));
      // Using nullptr cast for overload routing.
      return GetValue(column_value, static_cast<ValueType*>(nullptr));
    }
  }
  return UnexpectedPackedRowVersionStatus(*packed_row_version);
}

template <class ValueType>
Result<std::optional<ValueType>> GetColumnValueNotPacked(
    tablet::TableInfo* table_info, Slice value, const std::string& column_name,
    const dockv::SubDocKey& decoded_sub_doc_key) {
  SCHECK_EQ(decoded_sub_doc_key.subkeys().size(), 1U, Corruption, "Wrong number of subdoc keys");
  const auto& first_subkey = decoded_sub_doc_key.subkeys()[0];
  if (first_subkey.type() == dockv::KeyEntryType::kColumnId) {
    auto column_id = first_subkey.GetColumnId();
    const ColumnSchema& column = VERIFY_RESULT(table_info->schema().column_by_id(column_id));
    if (column.name() == column_name) {
      dockv::Value column_value;
      RETURN_NOT_OK(column_value.Decode(value));
      return GetValue(column_value.primitive_value(), static_cast<ValueType*>(nullptr));
    }
  }
  return std::nullopt;
}

} // namespace

Status FetchState::SetPrefix(const Slice& prefix) {
  if (prefix_.empty()) {
    iterator_->Seek(prefix);
  } else {
    iterator_->SeekForward(prefix);
  }
  prefix_ = prefix;
  finished_ = false;
  key_write_stack_.clear();
  num_rows_ = 0;
  return Next(MoveForward::kFalse);
}

Result<bool> FetchState::Update() {
  key_ = VERIFY_RESULT(iterator_->Fetch());
  if (!key_) {
    finished_ = true;
    return true;
  }
  auto rest_of_key = key_.key;
  if (!rest_of_key.starts_with(prefix_)) {
    finished_ = true;
    return true;
  }

  rest_of_key.remove_prefix(prefix_.size());
  const auto key_write_time = VERIFY_RESULT(key_.write_time.Decode());
  for (auto i = key_write_stack_.begin(); i != key_write_stack_.end(); ++i) {
    if (!rest_of_key.starts_with(i->key.AsSlice())) {
      key_write_stack_.erase(i, key_write_stack_.end());
      break;
    }
    if (i->time > key_write_time) {
      // This key-value entry is outdated and we should pick the next one.
      return false;
    }
    rest_of_key.remove_prefix(i->key.size());
  }

  auto alive_row = !VERIFY_RESULT(dockv::Value::IsTombstoned(value()));
  if (key_write_stack_.empty()) {
    // Empty stack means new row, i.e. doc key. So rest_of_key is not updated and matches
    // full key, that contains doc key.
    if (alive_row) {
      ++num_rows_;
    }
    auto doc_key_size = VERIFY_RESULT(
        dockv::DocKey::EncodedHashPartAndDocKeySizes(rest_of_key)).doc_key_size;
    key_write_stack_.push_back(KeyWriteEntry {
      .key = KeyBuffer(rest_of_key.Prefix(doc_key_size)),
      // If doc key does not have its own write time, then we use min time to avoid ignoring
      // updates for other columns.
      .time = doc_key_size == rest_of_key.size() ? key_write_time : DocHybridTime::kMin,
    });
    rest_of_key.remove_prefix(doc_key_size);
  }

  // See comment for key_write_stack_ field.
  if (!rest_of_key.empty()) {
    // If we have multiple subkeys in rest_of_key, it is NOT necessary to split them.
    // Since complete subkey cannot be prefix of another subkey.
    key_write_stack_.push_back(KeyWriteEntry {
      .key = KeyBuffer(rest_of_key),
      .time = key_write_time,
    });
  }

  return alive_row;
}

Status FetchState::Next(MoveForward move_forward) {
  while (!finished()) {
    if (move_forward) {
      iterator_->SeekPastSubKey(key_.key);
    } else {
      move_forward = MoveForward::kTrue;
    }
    if (VERIFY_RESULT(Update())) {
      break;
    }
  }
  return Status::OK();
}

Status RestorePatch::ProcessCommonEntry(
    const Slice& key, const Slice& existing_value, const Slice& restoring_value) {
  VLOG_WITH_FUNC(3) << "Key: " << key.ToDebugHexString() << ", existing value: "
                    << existing_value.ToDebugHexString() << ", restoring value: "
                    << restoring_value.ToDebugHexString();
  if (restoring_value.compare(existing_value)) {
    IncrementTicker(RestoreTicker::kUpdates);
    AddKeyValue(key, restoring_value, doc_batch_);
  }
  // If this is a packed row, update the state.
  return TryUpdateLastPackedRow(key, restoring_value);
}

Status RestorePatch::ProcessRestoringOnlyEntry(
    const Slice& restoring_key, const Slice& restoring_value) {
  VLOG_WITH_FUNC(3) << "Restoring key: " << restoring_key.ToDebugHexString() << ", "
                    << "restoring value: " << restoring_value.ToDebugHexString();
  IncrementTicker(RestoreTicker::kInserts);
  AddKeyValue(restoring_key, restoring_value, doc_batch_);
  // If this is a packed row, update the state.
  return TryUpdateLastPackedRow(restoring_key, restoring_value);
}

Status RestorePatch::ProcessExistingOnlyEntry(
    const Slice& existing_key, const Slice& existing_value) {
  VLOG_WITH_FUNC(3) << "Existing key: " << existing_key.ToDebugHexString() << ", "
                    << "existing value: " << existing_value.ToDebugHexString() << ", "
                    << "last packed row key: "
                    << last_packed_row_restoring_state_.key.AsSlice().ToDebugHexString()
                    << ", last packed row value: "
                    << last_packed_row_restoring_state_.value.AsSlice().ToDebugHexString();
  if (!last_packed_row_restoring_state_.key.empty() &&
      existing_key.starts_with(last_packed_row_restoring_state_.key.AsSlice())) {
    // Find out this column's value from the packed row.
    Slice subkey = existing_key.WithoutPrefix(last_packed_row_restoring_state_.key.size());
    if (!subkey.empty()) {
      char type = subkey.consume_byte();
      if (dockv::IsColumnId(static_cast<dockv::KeyEntryType>(type))) {
        if (!last_packed_row_restoring_state_.decoder) {
          Slice packed_value = last_packed_row_restoring_state_.value.AsSlice();
          auto packed_row_version = *dockv::GetPackedRowVersion(
              static_cast<dockv::ValueEntryType>(packed_value.consume_byte()));
          const dockv::SchemaPacking& packing = VERIFY_RESULT(
              table_info_->doc_read_context->schema_packing_storage.GetPacking(&packed_value));
          switch (packed_row_version) {
            case dockv::PackedRowVersion::kV1:
              last_packed_row_restoring_state_.decoder.emplace(
                  std::in_place_type_t<dockv::PackedRowDecoderV1>(), packing, packed_value.data());
              break;
            case dockv::PackedRowVersion::kV2:
              last_packed_row_restoring_state_.decoder.emplace(
                  std::in_place_type_t<dockv::PackedRowDecoderV2>(), packing, packed_value.data());
              break;
          }
        }
        int64_t column_id_as_int64 = VERIFY_RESULT(FastDecodeSignedVarIntUnsafe(&subkey));
        // Expect only one subkey.
        SCHECK_EQ(subkey.empty(), true, Corruption, "Only one subkey expected");
        ColumnId column_id;
        RETURN_NOT_OK(ColumnId::FromInt64(column_id_as_int64, &column_id));
        // Insert this column's packed row value.
        return std::visit([this, column_id, existing_key](auto& decoder) {
          auto index = decoder.packing().GetIndex(column_id);
          if (index == dockv::SchemaPacking::kSkippedColumnIdx) {
            return Status::OK();
          }
          auto value = decoder.FetchValue(index);
          VLOG_WITH_FUNC(1) << "Inserting key: " << existing_key.ToDebugHexString()
                            << ", value: " << value->ToDebugHexString();
          AddEntry(
              existing_key, value, decoder.packing().column_packing_data(index).data_type,
              doc_batch_);
          IncrementTicker(RestoreTicker::kInserts);
          return Status::OK();
        }, *last_packed_row_restoring_state_.decoder);
      }
    }
  }
  // Otherwise delete this kv.
  char tombstone_char = dockv::ValueEntryTypeAsChar::kTombstone;
  Slice tombstone(&tombstone_char, 1);
  IncrementTicker(RestoreTicker::kDeletes);
  AddKeyValue(existing_key, tombstone, doc_batch_);
  return Status::OK();
}

Status RestorePatch::PatchCurrentStateFromRestoringState() {
  while (restoring_state_ && existing_state_ && !restoring_state_->finished() &&
         !existing_state_->finished()) {
    if (VERIFY_RESULT(ShouldSkipEntry(restoring_state_->key(), restoring_state_->value()))) {
      RETURN_NOT_OK(restoring_state_->Next());
      continue;
    }
    if (VERIFY_RESULT(ShouldSkipEntry(existing_state_->key(), existing_state_->value()))) {
      RETURN_NOT_OK(existing_state_->Next());
      continue;
    }
    auto compare_result = restoring_state_->key().compare(existing_state_->key());
    if (compare_result == 0) {
      RETURN_NOT_OK(ProcessCommonEntry(
          existing_state_->key(), existing_state_->value(), restoring_state_->value()));
      RETURN_NOT_OK(restoring_state_->Next());
      RETURN_NOT_OK(existing_state_->Next());
    } else if (compare_result < 0) {
      RETURN_NOT_OK(ProcessRestoringOnlyEntry(
          restoring_state_->key(), restoring_state_->value()));
      RETURN_NOT_OK(restoring_state_->Next());
    } else {
      RETURN_NOT_OK(ProcessExistingOnlyEntry(
          existing_state_->key(), existing_state_->value()));
      RETURN_NOT_OK(existing_state_->Next());
    }
  }

  while (restoring_state_ && !restoring_state_->finished()) {
    if (VERIFY_RESULT(ShouldSkipEntry(restoring_state_->key(), restoring_state_->value()))) {
      RETURN_NOT_OK(restoring_state_->Next());
      continue;
    }
    RETURN_NOT_OK(ProcessRestoringOnlyEntry(
        restoring_state_->key(), restoring_state_->value()));
    RETURN_NOT_OK(restoring_state_->Next());
  }

  while (existing_state_ && !existing_state_->finished()) {
    if (VERIFY_RESULT(ShouldSkipEntry(existing_state_->key(), existing_state_->value()))) {
      RETURN_NOT_OK(existing_state_->Next());
      continue;
    }
    RETURN_NOT_OK(ProcessExistingOnlyEntry(
        existing_state_->key(), existing_state_->value()));
    RETURN_NOT_OK(existing_state_->Next());
  }

  return Status::OK();
}

Status RestorePatch::TryUpdateLastPackedRow(const Slice& key, const Slice& value) {
  VLOG_WITH_FUNC(3) << "Key: " << key.ToDebugHexString()
                    << ", value: " << value.ToDebugHexString();
  auto value_slice = value;
  RETURN_NOT_OK(dockv::ValueControlFields::Decode(&value_slice));
  auto value_type = dockv::DecodeValueEntryType(value_slice);
  if (IsPackedRow(value_type)) {
    VLOG_WITH_FUNC(2) << "Packed row encountered in the restoring state. Key: "
                      << key.ToDebugHexString() << ", value: " << value.ToDebugHexString();
    last_packed_row_restoring_state_.key = key;
    last_packed_row_restoring_state_.value = value_slice;
    last_packed_row_restoring_state_.decoder.reset();
  }
  return Status::OK();
}

void WriteToRocksDB(
    docdb::DocWriteBatch* write_batch, const HybridTime& write_time, const OpId& op_id,
    tablet::Tablet* tablet, const std::optional<docdb::KeyValuePairPB>& restore_kv) {
  auto kv_write_batch = rpc::MakeSharedMessage<docdb::LWKeyValueWriteBatchPB>();
  write_batch->MoveToWriteBatchPB(kv_write_batch.get());

  // Append restore entry to the write batch.
  if (restore_kv) {
    kv_write_batch->add_write_pairs()->CopyFrom(*restore_kv);
  }

  docdb::NonTransactionalWriter writer(*kv_write_batch, write_time);
  rocksdb::WriteBatch rocksdb_write_batch;
  rocksdb_write_batch.SetDirectWriter(&writer);
  docdb::ConsensusFrontiers frontiers;
  set_op_id(op_id, &frontiers);
  set_hybrid_time(write_time, &frontiers);

  tablet->WriteToRocksDB(
      &frontiers, &rocksdb_write_batch, docdb::StorageDbType::kRegular);
}

Result<std::optional<int64_t>> GetInt64ColumnValue(
    const dockv::SubDocKey& sub_doc_key, const Slice& value,
    tablet::TableInfo* table_info, const std::string& column_name) {
  // Packed row case.
  if (sub_doc_key.subkeys().empty()) {
    return VERIFY_RESULT(GetColumnValuePacked<int64_t>(table_info, value, column_name));
  }
  return VERIFY_RESULT(GetColumnValueNotPacked<int64_t>(
      table_info, value, column_name, sub_doc_key));
}

Result<std::optional<bool>> GetBoolColumnValue(
    const dockv::SubDocKey& sub_doc_key, const Slice& value,
    tablet::TableInfo* table_info, const std::string& column_name) {
  // Packed row case.
  if (sub_doc_key.subkeys().empty()) {
    return VERIFY_RESULT(GetColumnValuePacked<bool>(table_info, value, column_name));
  }
  return VERIFY_RESULT(GetColumnValueNotPacked<bool>(
      table_info, value, column_name, sub_doc_key));
}

} // namespace yb
