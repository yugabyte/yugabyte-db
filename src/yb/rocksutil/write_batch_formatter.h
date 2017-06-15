// Copyright (c) YugaByte, Inc.

#ifndef YB_ROCKSUTIL_WRITE_BATCH_FORMATTER_H
#define YB_ROCKSUTIL_WRITE_BATCH_FORMATTER_H

#include <sstream>

#include "rocksdb/status.h"
#include "rocksdb/types.h"
#include "rocksdb/write_batch.h"

namespace yb {

// Produces a human-readable representation of the given RocksDB WriteBatch, e.g.:
// <pre>
// 1. PutCF('key1', 'value1')
// 2. PutCF('key2', 'value2')
// </pre>
class WriteBatchFormatter : public rocksdb::WriteBatch::Handler {
 public:
  virtual rocksdb::Status PutCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key,
      const rocksdb::Slice& value) override;

  virtual rocksdb::Status DeleteCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key) override;

  virtual rocksdb::Status SingleDeleteCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key) override;

  virtual rocksdb::Status MergeCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key,
      const rocksdb::Slice& value) override;

  Status UserOpId(const OpId& op_id) override;

  std::string str() { return out_.str(); }

 private:

  void StartOutputLine(const char* name);
  void OutputField(const rocksdb::Slice& value);
  void FinishOutputLine();

  bool need_separator_ = false;
  std::stringstream out_;
  int update_index_ = 0;
};

} // namespace yb

#endif // YB_ROCKSUTIL_WRITE_BATCH_FORMATTER_H
