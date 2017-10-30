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

#include <iostream>

#include "yb/rocksdb/util/statistics.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_util.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/tablet/tablet_options.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/string_trim.h"

using std::string;
using std::make_shared;
using std::endl;
using strings::Substitute;
using yb::util::FormatBytesAsStr;
using yb::util::ApplyEagerLineContinuation;
using std::vector;

namespace yb {
namespace docdb {

// Add primary key column values to the component group. Verify that they are in the same order
// as in the table schema.
CHECKED_STATUS QLKeyColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<QLExpressionPB> &column_values,
    const Schema &schema, size_t column_idx, const size_t column_count,
    vector<PrimitiveValue> *components) {
  for (const auto& column_value : column_values) {
    DCHECK(schema.is_key_column(column_idx));
    if (!column_value.has_value() || QLValue::IsNull(column_value.value())) {
      return STATUS(InvalidArgument, "Invalid primary key value");
    }

    components->push_back(PrimitiveValue::FromQLExpressionPB(
        column_value, schema.column(column_idx).sorting_type()));
    column_idx++;
  }
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------

DocDBRocksDBUtil::DocDBRocksDBUtil()
    : DocDBRocksDBUtil(OpId(1, 42)) {
}

DocDBRocksDBUtil::DocDBRocksDBUtil(const rocksdb::OpId& op_id)
    : op_id_(op_id),
      retention_policy_(make_shared<FixedHybridTimeRetentionPolicy>(HybridTime::kMin,
                                                                    MonoDelta::kMax)) {
}

DocDBRocksDBUtil::~DocDBRocksDBUtil() {
}

rocksdb::DB* DocDBRocksDBUtil::rocksdb() {
  return DCHECK_NOTNULL(rocksdb_.get());
}

void DocDBRocksDBUtil::SetRocksDBDir(const std::string& rocksdb_dir) {
  rocksdb_dir_ = rocksdb_dir;
}

Status DocDBRocksDBUtil::OpenRocksDB() {
  // Init the directory if needed.
  if (rocksdb_dir_.empty()) {
    RETURN_NOT_OK(InitRocksDBDir());
  }

  rocksdb::DB* rocksdb = nullptr;
  RETURN_NOT_OK(rocksdb::DB::Open(rocksdb_options_, rocksdb_dir_, &rocksdb));
  LOG(INFO) << "Opened RocksDB at " << rocksdb_dir_;
  rocksdb_.reset(rocksdb);
  return Status::OK();
}

Status DocDBRocksDBUtil::ReopenRocksDB() {
  rocksdb_.reset();
  return OpenRocksDB();
}

Status DocDBRocksDBUtil::DestroyRocksDB() {
  rocksdb_.reset();
  LOG(INFO) << "Destroying RocksDB database at " << rocksdb_dir_;
  return rocksdb::DestroyDB(rocksdb_dir_, rocksdb_options_);
}

void DocDBRocksDBUtil::ResetMonotonicCounter() {
  monotonic_counter_.store(0);
}

Status DocDBRocksDBUtil::PopulateRocksDBWriteBatch(
    const DocWriteBatch& dwb,
    rocksdb::WriteBatch *rocksdb_write_batch,
    HybridTime hybrid_time,
    bool decode_dockey,
    bool increment_write_id) const {
  for (const auto& entry : dwb.key_value_pairs()) {
    if (decode_dockey) {
      SubDocKey subdoc_key;
      // We don't expect any invalid encoded keys in the write batch. However, these encoded keys
      // don't contain the HybridTime.
      RETURN_NOT_OK_PREPEND(subdoc_key.FullyDecodeFromKeyWithoutHybridTime(entry.first),
          Substitute("when decoding key: $0", FormatBytesAsStr(entry.first)));
    }
  }

  if (current_txn_id_.is_initialized()) {
    if (!increment_write_id) {
      return STATUS(
          InternalError, "For transactional write only increment_write_id=true is supported");
    }
    KeyValueWriteBatchPB kv_write_batch;
    dwb.TEST_CopyToWriteBatchPB(&kv_write_batch);
    PrepareTransactionWriteBatch(
        kv_write_batch, hybrid_time, rocksdb_write_batch, *current_txn_id_, txn_isolation_level_);
  } else {
    // TODO: this block has common code with docdb::PrepareNonTransactionWriteBatch and probably
    // can be refactored, so common code is reused.
    IntraTxnWriteId write_id = 0;
    for (const auto& entry : dwb.key_value_pairs()) {
      string rocksdb_key;
      if (hybrid_time.is_valid()) {
        // HybridTime provided. Append a PrimitiveValue with the HybridTime to the key.
        const KeyBytes encoded_ht =
            PrimitiveValue(DocHybridTime(hybrid_time, write_id)).ToKeyBytes();
        rocksdb_key = entry.first + encoded_ht.data();
      } else {
        // Useful when printing out a write batch that does not yet know the HybridTime it will be
        // committed with.
        rocksdb_key = entry.first;
      }
      rocksdb_write_batch->Put(rocksdb_key, entry.second);
      if (increment_write_id) {
        ++write_id;
      }
    }
  }
  return Status::OK();
}

Status DocDBRocksDBUtil::WriteToRocksDB(
    const DocWriteBatch& doc_write_batch,
    const HybridTime& hybrid_time,
    bool decode_dockey,
    bool increment_write_id) {
  if (doc_write_batch.IsEmpty()) {
    return Status::OK();
  }
  if (!hybrid_time.is_valid()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Hybrid time is not valid: $0",
                             hybrid_time.ToString());
  }

  rocksdb::WriteBatch rocksdb_write_batch;
  if (op_id_) {
    ++op_id_.index;
    rocksdb_write_batch.SetUserOpId(op_id_);
  }

  RETURN_NOT_OK(PopulateRocksDBWriteBatch(
      doc_write_batch, &rocksdb_write_batch, hybrid_time, decode_dockey, increment_write_id));

  rocksdb::Status rocksdb_write_status = rocksdb_->Write(write_options(), &rocksdb_write_batch);

  if (!rocksdb_write_status.ok()) {
    LOG(ERROR) << "Failed writing to RocksDB: " << rocksdb_write_status.ToString();
    return STATUS_SUBSTITUTE(RuntimeError,
                             "Error writing to RocksDB: $0", rocksdb_write_status.ToString());
  }
  return Status::OK();
}

Status DocDBRocksDBUtil::InitCommonRocksDBOptions() {
  // TODO(bojanserafimov): create MemoryMonitor?
  const size_t cache_size = block_cache_size();
  if (cache_size > 0) {
    block_cache_ = rocksdb::NewLRUCache(cache_size);
  }

  tablet::TabletOptions tablet_options;
  tablet_options.block_cache = block_cache_;
  docdb::InitRocksDBOptions(&rocksdb_options_, tablet_id(), rocksdb::CreateDBStatistics(),
                            tablet_options);
  InitRocksDBWriteOptions(&write_options_);
  rocksdb_options_.compaction_filter_factory =
      std::make_shared<docdb::DocDBCompactionFilterFactory>(retention_policy_);
  return Status::OK();
}

Status DocDBRocksDBUtil::WriteToRocksDBAndClear(
    DocWriteBatch* dwb,
    const HybridTime& hybrid_time,
    bool decode_dockey, bool increment_write_id) {
  RETURN_NOT_OK(WriteToRocksDB(*dwb, hybrid_time, decode_dockey, increment_write_id));
  dwb->Clear();
  return Status::OK();
}

void DocDBRocksDBUtil::SetHistoryCutoffHybridTime(HybridTime history_cutoff) {
  retention_policy_->SetHistoryCutoff(history_cutoff);
}

void DocDBRocksDBUtil::SetTableTTL(uint64_t ttl_msec) {
  schema_.SetDefaultTimeToLive(ttl_msec);
  retention_policy_->SetTableTTLForTests(MonoDelta::FromMilliseconds(ttl_msec));
}

string DocDBRocksDBUtil::DocDBDebugDumpToStr() {
  return yb::docdb::DocDBDebugDumpToStr(rocksdb());
}

Status DocDBRocksDBUtil::SetPrimitive(
    const DocPath& doc_path,
    const Value& value,
    const HybridTime hybrid_time,
    InitMarkerBehavior use_init_marker) {
  DocWriteBatch local_doc_write_batch(rocksdb_.get(), &monotonic_counter_);
  RETURN_NOT_OK(local_doc_write_batch.SetPrimitive(doc_path, value, use_init_marker));
  return WriteToRocksDB(local_doc_write_batch, hybrid_time);
}

Status DocDBRocksDBUtil::SetPrimitive(
    const DocPath& doc_path,
    const PrimitiveValue& primitive_value,
    const HybridTime hybrid_time,
    InitMarkerBehavior use_init_marker) {
  return SetPrimitive(doc_path, Value(primitive_value), hybrid_time, use_init_marker);
}

Status DocDBRocksDBUtil::InsertSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    const HybridTime hybrid_time,
    InitMarkerBehavior use_init_marker) {
  DocWriteBatch dwb(rocksdb_.get(), &monotonic_counter_);
  RETURN_NOT_OK(dwb.InsertSubDocument(doc_path, value, use_init_marker));
  return WriteToRocksDB(dwb, hybrid_time);
}

Status DocDBRocksDBUtil::ExtendSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    const HybridTime hybrid_time,
    InitMarkerBehavior use_init_marker) {
  DocWriteBatch dwb(rocksdb_.get(), &monotonic_counter_);
  RETURN_NOT_OK(dwb.ExtendSubDocument(doc_path, value, use_init_marker));
  return WriteToRocksDB(dwb, hybrid_time);
}

Status DocDBRocksDBUtil::ExtendList(
    const DocPath& doc_path,
    const SubDocument& value,
    const ListExtendOrder extend_order,
    HybridTime hybrid_time,
    InitMarkerBehavior use_init_marker) {
  DocWriteBatch dwb(rocksdb_.get(), &monotonic_counter_);
  RETURN_NOT_OK(dwb.ExtendList(doc_path, value, extend_order, use_init_marker));
  return WriteToRocksDB(dwb, hybrid_time);
}

Status DocDBRocksDBUtil::ReplaceInList(
    const DocPath &doc_path,
    const std::vector<int>& indexes,
    const std::vector<SubDocument>& values,
    const HybridTime& current_time,
    const HybridTime& hybrid_time,
    const rocksdb::QueryId query_id,
    MonoDelta table_ttl,
    MonoDelta ttl,
    InitMarkerBehavior use_init_marker) {
  DocWriteBatch dwb(rocksdb_.get(), &monotonic_counter_);
  RETURN_NOT_OK(dwb.ReplaceInList(
      doc_path, indexes, values, current_time, query_id, table_ttl, ttl, use_init_marker));
  return WriteToRocksDB(dwb, hybrid_time);
}

Status DocDBRocksDBUtil::DeleteSubDoc(
    const DocPath& doc_path,
    HybridTime hybrid_time,
    InitMarkerBehavior use_init_marker) {
  DocWriteBatch dwb(rocksdb(), &monotonic_counter_);
  RETURN_NOT_OK(dwb.DeleteSubDoc(doc_path, use_init_marker));
  return WriteToRocksDB(dwb, hybrid_time);
}

Status DocDBRocksDBUtil::DocDBDebugDumpToConsole() {
  return DocDBDebugDump(rocksdb_.get(), std::cerr);
}

Status DocDBRocksDBUtil::FlushRocksDB() {
  rocksdb::FlushOptions flush_options;
  return rocksdb()->Flush(flush_options);
}

Status DocDBRocksDBUtil::ReinitDBOptions() {
  tablet::TabletOptions tablet_options;
  docdb::InitRocksDBOptions(&rocksdb_options_, tablet_id(), rocksdb_options_.statistics,
                            tablet_options);
  return ReopenRocksDB();
}

// ------------------------------------------------------------------------------------------------

DebugDocVisitor::DebugDocVisitor() {
}

DebugDocVisitor::~DebugDocVisitor() {
}

#define SIMPLE_DEBUG_DOC_VISITOR_METHOD(method_name) \
  Status DebugDocVisitor::method_name() { \
    out_ << BOOST_PP_STRINGIZE(method_name) << endl; \
    return Status::OK(); \
  }

#define SIMPLE_DEBUG_DOC_VISITOR_METHOD_ARGUMENT(method_name, argument_type) \
  Status DebugDocVisitor::method_name(const argument_type& arg) { \
    out_ << BOOST_PP_STRINGIZE(method_name) << "(" << arg << ")" << std::endl; \
    return Status::OK(); \
  }

SIMPLE_DEBUG_DOC_VISITOR_METHOD_ARGUMENT(StartSubDocument, SubDocKey);
SIMPLE_DEBUG_DOC_VISITOR_METHOD_ARGUMENT(VisitKey, PrimitiveValue);
SIMPLE_DEBUG_DOC_VISITOR_METHOD_ARGUMENT(VisitValue, PrimitiveValue);
SIMPLE_DEBUG_DOC_VISITOR_METHOD(EndSubDocument)
SIMPLE_DEBUG_DOC_VISITOR_METHOD(StartObject)
SIMPLE_DEBUG_DOC_VISITOR_METHOD(EndObject)
SIMPLE_DEBUG_DOC_VISITOR_METHOD(StartArray)
SIMPLE_DEBUG_DOC_VISITOR_METHOD(EndArray)

string DebugDocVisitor::ToString() {
  return out_.str();
}

#undef SIMPLE_DEBUG_DOC_VISITOR_METHOD

}  // namespace docdb
}  // namespace yb
