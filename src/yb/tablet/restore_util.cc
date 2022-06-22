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

namespace yb {

Status FetchState::SetPrefix(const Slice& prefix) {
  if (prefix_.empty()) {
    iterator_->Seek(prefix);
  } else {
    iterator_->SeekForward(prefix);
  }
  prefix_ = prefix;
  finished_ = false;
  last_deleted_key_bytes_.clear();
  last_deleted_key_write_time_ = DocHybridTime::kInvalid;
  RETURN_NOT_OK(Update());
  return NextNonDeletedEntry();
}

Result<bool> FetchState::IsDeletedRowEntry() {
  // Because Postgres doesn't have a concept of frozen types, kGroupEnd will only demarcate the
  // end of hashed and range components. It is reasonable to assume then that if the last byte
  // is kGroupEnd then it does not have any subkeys.
  bool no_subkey =
      key()[key().size() - 1] == docdb::KeyEntryTypeAsChar::kGroupEnd;

  return no_subkey && VERIFY_RESULT(IsDeletedEntry());
}

Result<bool> FetchState::IsDeletedEntry() {
  return VERIFY_RESULT(docdb::Value::IsTombstoned(value()));
}

bool FetchState::IsDeletedSinceInsertion() {
  if (last_deleted_key_bytes_.size() == 0) {
    return false;
  }
  return key().starts_with(last_deleted_key_bytes_.AsSlice()) &&
          FullKey().write_time < last_deleted_key_write_time_;
}

Status FetchState::Update() {
  if (!iterator_->valid()) {
    finished_ = true;
    return Status::OK();
  }
  key_ = VERIFY_RESULT(iterator_->FetchKey());
  if (VERIFY_RESULT(IsDeletedRowEntry())) {
    last_deleted_key_write_time_ = key_.write_time;
    last_deleted_key_bytes_ = key_.key;
  }
  if (!key_.key.starts_with(prefix_)) {
    finished_ = true;
    return Status::OK();
  }

  return Status::OK();
}

Status FetchState::NextNonDeletedEntry() {
  while (!finished()) {
    if (VERIFY_RESULT(IsDeletedEntry()) || IsDeletedSinceInsertion()) {
      RETURN_NOT_OK(NextEntry());
      continue;
    }
    break;
  }
  return Status::OK();
}

Status RestorePatch::ProcessEqualEntries(
    const Slice& existing_key, const Slice& existing_value,
    const Slice& restoring_key, const Slice& restoring_value) {
  if (restoring_value.compare(existing_value)) {
    IncrementTicker(RestoreTicker::kUpdates);
    AddKeyValue(restoring_key, restoring_value, doc_batch_);
  }
  return Status::OK();
}

Status RestorePatch::ProcessRestoringLessThanExisting(
    const Slice& existing_key, const Slice& existing_value,
    const Slice& restoring_key, const Slice& restoring_value) {
  IncrementTicker(RestoreTicker::kInserts);
  AddKeyValue(restoring_key, restoring_value, doc_batch_);
  return Status::OK();
}

Status RestorePatch::ProcessRestoringGreaterThanExisting(
    const Slice& existing_key, const Slice& existing_value,
    const Slice& restoring_key, const Slice& restoring_value) {
  char tombstone_char = docdb::ValueEntryTypeAsChar::kTombstone;
  Slice tombstone(&tombstone_char, 1);
  IncrementTicker(RestoreTicker::kDeletes);
  AddKeyValue(existing_key, tombstone, doc_batch_);
  return Status::OK();
}

Status RestorePatch::PatchCurrentStateFromRestoringState() {
  while (!restoring_state_->finished() && !existing_state_->finished()) {
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
      RETURN_NOT_OK(ProcessEqualEntries(existing_state_->key(), existing_state_->value(),
                                        restoring_state_->key(), restoring_state_->value()));
      RETURN_NOT_OK(restoring_state_->Next());
      RETURN_NOT_OK(existing_state_->Next());
    } else if (compare_result < 0) {
      RETURN_NOT_OK(ProcessRestoringLessThanExisting(
          existing_state_->key(), existing_state_->value(),
          restoring_state_->key(), restoring_state_->value()));
      RETURN_NOT_OK(restoring_state_->Next());
    } else {
      RETURN_NOT_OK(ProcessRestoringGreaterThanExisting(
          existing_state_->key(), existing_state_->value(),
          restoring_state_->key(), restoring_state_->value()));
      RETURN_NOT_OK(existing_state_->Next());
    }
  }

  while (!restoring_state_->finished()) {
    if (VERIFY_RESULT(ShouldSkipEntry(restoring_state_->key(), restoring_state_->value()))) {
      RETURN_NOT_OK(restoring_state_->Next());
      continue;
    }
    RETURN_NOT_OK(ProcessRestoringLessThanExisting(
        Slice(), Slice(), restoring_state_->key(), restoring_state_->value()));
    RETURN_NOT_OK(restoring_state_->Next());
  }

  while (!existing_state_->finished()) {
    if (VERIFY_RESULT(ShouldSkipEntry(existing_state_->key(), existing_state_->value()))) {
      RETURN_NOT_OK(existing_state_->Next());
      continue;
    }
    RETURN_NOT_OK(ProcessRestoringGreaterThanExisting(
        existing_state_->key(), existing_state_->value(), Slice(), Slice()));
    RETURN_NOT_OK(existing_state_->Next());
  }

  return Status::OK();
}

void AddKeyValue(const Slice& key, const Slice& value, docdb::DocWriteBatch* write_batch) {
  auto& pair = write_batch->AddRaw();
  pair.key.assign(key.cdata(), key.size());
  pair.value.assign(value.cdata(), value.size());
}

void WriteToRocksDB(
    docdb::DocWriteBatch* write_batch, const HybridTime& write_time, const OpId& op_id,
    tablet::Tablet* tablet, const std::optional<docdb::KeyValuePairPB>& restore_kv) {
  docdb::KeyValueWriteBatchPB kv_write_batch;
  write_batch->MoveToWriteBatchPB(&kv_write_batch);

  // Append restore entry to the write batch.
  if (restore_kv) {
    *kv_write_batch.mutable_write_pairs()->Add() = *restore_kv;
  }

  docdb::NonTransactionalWriter writer(kv_write_batch, write_time);
  rocksdb::WriteBatch rocksdb_write_batch;
  rocksdb_write_batch.SetDirectWriter(&writer);
  docdb::ConsensusFrontiers frontiers;
  set_op_id(op_id, &frontiers);
  set_hybrid_time(write_time, &frontiers);

  tablet->WriteToRocksDB(
      &frontiers, &rocksdb_write_batch, docdb::StorageDbType::kRegular);
}
} // namespace yb
