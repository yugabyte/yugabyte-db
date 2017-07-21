// Copyright (c) YugaByte, Inc.

// Utilities for docdb operations.

#ifndef YB_DOCDB_DOCDB_UTIL_H
#define YB_DOCDB_DOCDB_UTIL_H

#include "yb/common/schema.h"
#include "yb/common/yql_value.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

// Add primary key column values to the component group. Verify that they are in the same order
// as in the table schema.
CHECKED_STATUS YQLKeyColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<YQLColumnValuePB> &column_values,
    const Schema &schema, size_t column_idx, const size_t column_count,
    vector<PrimitiveValue> *components);

// A wrapper around a RocksDB instance and provides utility functions on top of it, such as
// compacting the history until a certain point. This is also a convenient base class for GTest test
// classes, because it exposes member functions such as rocksdb() and write_oiptions().
class DocDBRocksDBUtil {

 public:
  DocDBRocksDBUtil();
  explicit DocDBRocksDBUtil(const rocksdb::OpId& op_id);
  virtual ~DocDBRocksDBUtil();
  virtual CHECKED_STATUS InitRocksDBDir() = 0;
  // Initializes RocksDB options, should be called after constructor, because it uses virtual
  // function BlockCacheSize.
  virtual CHECKED_STATUS InitRocksDBOptions() = 0;
  virtual std::string tablet_id() = 0;

  // Size of block cache for RocksDB, 0 means don't use block cache.
  virtual size_t block_cache_size() const { return 16 * 1024 * 1024; }

  rocksdb::DB* rocksdb();

  CHECKED_STATUS InitCommonRocksDBOptions();

  const rocksdb::WriteOptions& write_options() const { return write_options_; }

  const rocksdb::Options& options() const { return rocksdb_options_; }

  void SetRocksDBDir(const std::string& rocksdb_dir);
  CHECKED_STATUS OpenRocksDB();
  CHECKED_STATUS ReopenRocksDB();
  CHECKED_STATUS DestroyRocksDB();
  void ResetMonotonicCounter();

  // Populates the given RocksDB write batch from the given DocWriteBatch. If a valid hybrid_time is
  // specified, it is appended to every key.
  CHECKED_STATUS PopulateRocksDBWriteBatch(
      const DocWriteBatch& dwb,
      rocksdb::WriteBatch *rocksdb_write_batch,
      HybridTime hybrid_time = HybridTime::kInvalidHybridTime,
      bool decode_dockey = true,
      bool increment_write_id = true) const;

  // Writes the given DocWriteBatch to RocksDB. We substitue the hybrid time, if provided.
  CHECKED_STATUS WriteToRocksDB(const DocWriteBatch& write_batch, const HybridTime& hybrid_time,
                                bool decode_dockey = true, bool increment_write_id = true);

  // The same as WriteToRocksDB but also clears the write batch afterwards.
  CHECKED_STATUS WriteToRocksDBAndClear(DocWriteBatch* dwb, const HybridTime& hybrid_time,
                                        bool decode_dockey = true, bool increment_write_id = true);

  void SetHistoryCutoffHybridTime(HybridTime history_cutoff);

  // Produces a string listing the contents of the entire RocksDB database, with every key and value
  // decoded as a DocDB key/value and converted to a human-readable string representation.
  std::string DocDBDebugDumpToStr();

  // ----------------------------------------------------------------------------------------------
  // SetPrimitive taking a Value

  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path,
      const Value& value,
      HybridTime hybrid_time,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED);

  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path,
      const PrimitiveValue& value,
      HybridTime hybrid_time,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED);

  CHECKED_STATUS InsertSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      HybridTime hybrid_time,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL);

  CHECKED_STATUS ExtendSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      HybridTime hybrid_time,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL);

  CHECKED_STATUS ExtendList(
      const DocPath& doc_path,
      const SubDocument& value,
      const ListExtendOrder extend_order,
      HybridTime hybrid_time,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL);

  CHECKED_STATUS ReplaceInList(
      const DocPath &doc_path,
      const std::vector<int>& indexes,
      const std::vector<SubDocument>& values,
      const HybridTime& current_time, // Used for reading.
      const HybridTime& hybrid_time,
      const rocksdb::QueryId query_id,
      MonoDelta table_ttl = Value::kMaxTtl,
      MonoDelta ttl = Value::kMaxTtl,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL);

  CHECKED_STATUS DeleteSubDoc(
      const DocPath& doc_path,
      HybridTime hybrid_time,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED);

  CHECKED_STATUS DocDBDebugDumpToConsole();

  CHECKED_STATUS FlushRocksDB();

  void SetTableTTL(uint64_t ttl_msec);

  CHECKED_STATUS DisableCompactions() {
    rocksdb_options_.compaction_style = rocksdb::kCompactionStyleNone;
    return ReopenRocksDB();
  }

  CHECKED_STATUS ReinitDBOptions();

  std::atomic<int64_t>& monotonic_counter() {
    return monotonic_counter_;
  }

 protected:
  std::unique_ptr<rocksdb::DB> rocksdb_;
  rocksdb::Options rocksdb_options_;
  string rocksdb_dir_;
  rocksdb::OpId op_id_ = {1, 42};
  std::shared_ptr<rocksdb::Cache> block_cache_;
  std::shared_ptr<FixedHybridTimeRetentionPolicy> retention_policy_;
  rocksdb::WriteOptions write_options_;
  Schema schema_;

 private:
  std::atomic<int64_t> monotonic_counter_;
};

// An implementation of the document node visitor interface that dumps all events (document
// start/end, object keys and values, etc.) to a string as separate lines.
class DebugDocVisitor : public DocVisitor {
 public:
  DebugDocVisitor();
  virtual ~DebugDocVisitor();

  CHECKED_STATUS StartSubDocument(const SubDocKey &key) override;

  CHECKED_STATUS VisitKey(const PrimitiveValue& key) override;
  CHECKED_STATUS VisitValue(const PrimitiveValue& value) override;

  CHECKED_STATUS EndSubDocument() override;
  CHECKED_STATUS StartObject() override;
  CHECKED_STATUS EndObject() override;
  CHECKED_STATUS StartArray() override;
  CHECKED_STATUS EndArray() override;

  std::string ToString();

 private:
  std::stringstream out_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOCDB_UTIL_H
