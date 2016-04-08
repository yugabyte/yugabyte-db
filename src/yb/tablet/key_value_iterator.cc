// Copyright (c) YugaByte, Inc.

#include "yb/tablet/key_value_iterator.h"
#include "yb/common/scan_spec.h"

namespace yb {
namespace tablet {

KeyValueIterator::KeyValueIterator(
    const Schema* projection,
    MvccSnapshot mvcc_snap,
    rocksdb::DB* db)
  : projection_(projection),
    mvcc_snap_(std::move(mvcc_snap)),
    db_(db),
    has_upper_bound_key_(false),
    exclusive_upper_bound_key_(""),
    done_() {
}

KeyValueIterator::~KeyValueIterator() {
}

Status KeyValueIterator::Init(ScanSpec* spec) {
  rocksdb::ReadOptions read_options;
  db_iter_.reset(db_->NewIterator(read_options));
  if (spec->lower_bound_key() != nullptr) {
    const Slice& encoded_key(spec->lower_bound_key()->encoded_key());
    db_iter_->Seek(rocksdb::Slice(
      reinterpret_cast<const char* const>(encoded_key.data()), encoded_key.size()
    ));
  } else {
    db_iter_->SeekToFirst();
  }
  if (spec->exclusive_upper_bound_key() != nullptr) {
    const Slice& encoded_exclusive_upper_bound_key(
      spec->exclusive_upper_bound_key()->encoded_key());
    has_upper_bound_key_ = true;
    exclusive_upper_bound_key_ = encoded_exclusive_upper_bound_key.ToString();
  } else {
    has_upper_bound_key_ = false;

  }
  return Status::OK();
}

bool KeyValueIterator::HasNext() const {
  if (done_) {
    return false;
  }
  if (db_iter_->Valid()) {
    if (has_upper_bound_key_) {
      // TODO: there must be a reusable way to compare two byte arrays.
      rocksdb::Slice rocksdb_key(db_iter_->key());
      const char* const upper_bound_data = exclusive_upper_bound_key_.data();
      size_t upper_bound_size = exclusive_upper_bound_key_.size();
      int memcmp_result = memcmp(rocksdb_key.data(), upper_bound_data,
        min(rocksdb_key.size(), upper_bound_size));

      bool result = memcmp_result < 0 ||
        (memcmp_result == 0 && rocksdb_key.size() < upper_bound_size);
      if (!result) {
        done_ = true;
      }
      return result;
    } else {
      return true;
    }
  } else {
    done_ = true;
    return false;
  }
}

Status KeyValueIterator::NextBlock(RowBlock *dst) {
  // The following checks are similar to MemRowSet::Iterator::NextBlock.
  if (PREDICT_FALSE(!db_iter_->Valid())) {
    dst->Resize(0);
    return Status::NotFound("end of iter");
  }
  if (PREDICT_FALSE(dst->row_capacity() == 0)) {
    return Status::OK();
  }

  rocksdb::Slice rocksdb_key(db_iter_->key());
  rocksdb::Slice rocksdb_value(db_iter_->value());
  Slice key(rocksdb_key.data(), rocksdb_key.size());
  Slice value(rocksdb_value.data(), rocksdb_value.size());

  Slice key_copy;
  if (PREDICT_FALSE(!dst->arena()->RelocateSlice(key, &key_copy))) {
    return Status::IOError("out of memory");
  }
  Slice value_copy;
  if (PREDICT_FALSE(!dst->arena()->RelocateSlice(value, &value_copy))) {
    return Status::IOError("out of memory");
  }

  dst->Resize(1);
  dst->selection_vector()->SetAllTrue();

  RowBlockRow dst_row(dst->row(0));
  *((Slice*) dst_row.cell(0).mutable_ptr()) = key_copy;
  *((Slice*) dst_row.cell(1).mutable_ptr()) = value_copy;

  db_iter_->Next();

  return Status::OK();
}

void KeyValueIterator::GetIteratorStats(std::vector<IteratorStats>* stats) const {
  // Not implemented yet. We don't print warnings or error out here because this method is actually
  // being called.
}

}
}