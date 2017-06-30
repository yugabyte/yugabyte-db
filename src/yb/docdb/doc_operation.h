// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_OPERATION_H_
#define YB_DOCDB_DOC_OPERATION_H_

#include <list>

#include "rocksdb/db.h"

#include "yb/common/yql_storage_interface.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/primitive_value.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_rowblock.h"

namespace yb {
namespace docdb {

class DocWriteBatch;

class DocOperation {
 public:
  virtual ~DocOperation() {}

  // Does the operation require a read snapshot to be taken before being applied? If so, a
  // clean snapshot hybrid_time will be supplied when Apply() is called. For example,
  // YQLWriteOperation for a DML with a "... IF <condition> ..." clause needs to read the row to
  // evaluate the condition before the write and needs a read snapshot for a consistent read.
  virtual bool RequireReadSnapshot() const = 0;
  virtual std::list<DocPath> DocPathsToLock() const = 0;
  virtual CHECKED_STATUS Apply(
      DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) = 0;
};

class KuduWriteOperation: public DocOperation {
 public:
  KuduWriteOperation(DocPath doc_path, PrimitiveValue value) : doc_path_(doc_path), value_(value) {
  }

  bool RequireReadSnapshot() const override { return false; }

  std::list<DocPath> DocPathsToLock() const override;

  CHECKED_STATUS Apply(
      DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) override;

 private:
  DocPath doc_path_;
  PrimitiveValue value_;
};

class RedisWriteOperation: public DocOperation {
 public:
  RedisWriteOperation(const yb::RedisWriteRequestPB& request, HybridTime read_hybrid_time)
      : request_(request), response_(), read_hybrid_time_(read_hybrid_time) {}

  bool RequireReadSnapshot() const override { return false; }

  CHECKED_STATUS Apply(
      DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) override;

  std::list<DocPath> DocPathsToLock() const override;

  const RedisResponsePB &response();

 private:
  Status ApplySet(DocWriteBatch *doc_write_batch);
  Status ApplyGetSet(DocWriteBatch *doc_write_batch);
  Status ApplyAppend(DocWriteBatch *doc_write_batch);
  Status ApplyDel(DocWriteBatch *doc_write_batch);
  Status ApplySetRange(DocWriteBatch *doc_write_batch);
  Status ApplyIncr(DocWriteBatch *doc_write_batch, int64_t incr = 1);
  Status ApplyPush(DocWriteBatch *doc_write_batch);
  Status ApplyInsert(DocWriteBatch *doc_write_batch);
  Status ApplyPop(DocWriteBatch *doc_write_batch);
  Status ApplyAdd(DocWriteBatch *doc_write_batch);
  Status ApplyRemove(DocWriteBatch *doc_write_batch);

  const RedisWriteRequestPB& request_;
  RedisResponsePB response_;
  HybridTime read_hybrid_time_;
};

class RedisReadOperation {
 public:
  explicit RedisReadOperation(const yb::RedisReadRequestPB& request) : request_(request) {}

  CHECKED_STATUS Execute(rocksdb::DB *rocksdb, const HybridTime& hybrid_time);

  const RedisResponsePB &response();

 private:
  int ApplyIndex(int32_t index, const int32_t len);
  Status ExecuteGet(rocksdb::DB *rocksdb, HybridTime hybrid_time);
  Status ExecuteStrLen(rocksdb::DB *rocksdb, HybridTime hybrid_time);
  Status ExecuteExists(rocksdb::DB *rocksdb, HybridTime hybrid_time);
  Status ExecuteGetRange(rocksdb::DB *rocksdb, HybridTime hybrid_time);

  const RedisReadRequestPB& request_;
  RedisResponsePB response_;
};

class YQLWriteOperation : public DocOperation {
 public:
  YQLWriteOperation(
      const YQLWriteRequestPB& request, const Schema& schema, YQLResponsePB* response);

  bool RequireReadSnapshot() const override;

  std::list<DocPath> DocPathsToLock() const override;

  CHECKED_STATUS Apply(
      DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) override;

  const YQLWriteRequestPB& request() const { return request_; }
  YQLResponsePB* response() const { return response_; }

  // Rowblock to return the "[applied]" status for conditional DML.
  const YQLRowBlock* rowblock() const { return rowblock_.get(); }

 private:
  // Initialize hashed_doc_key_ and/or pk_doc_key_.
  CHECKED_STATUS InitializeKeys(bool hashed_key, bool primary_key);

  CHECKED_STATUS ReadColumns(rocksdb::DB *rocksdb,
                             const HybridTime& hybrid_time,
                             Schema *static_projection,
                             Schema *non_static_projection,
                             YQLValueMap *value_map,
                             const rocksdb::QueryId query_id);

  CHECKED_STATUS IsConditionSatisfied(const YQLConditionPB& condition,
                                      rocksdb::DB *rocksdb,
                                      const HybridTime& hybrid_time,
                                      bool* should_apply,
                                      std::unique_ptr<YQLRowBlock>* rowblock,
                                      YQLValueMap *value_map,
                                      const rocksdb::QueryId query_id);

  const Schema& schema_;

  // Doc key and doc path for hashed key (i.e. without range columns). Present when there is a
  // static column being written.
  std::unique_ptr<DocKey> hashed_doc_key_;
  std::unique_ptr<DocPath> hashed_doc_path_;

  // Doc key and doc path for primary key (i.e. with range columns). Present when there is a
  // non-static column being written or when writing the primary key alone (i.e. range columns are
  // present or table does not have range columns).
  std::unique_ptr<DocKey> pk_doc_key_;
  std::unique_ptr<DocPath> pk_doc_path_;

  const YQLWriteRequestPB& request_;
  YQLResponsePB* response_;
  // The row and the column schema that is returned to the CQL client for an INSERT/UPDATE/DELETE
  // that has a "... IF <condition> ..." clause. The row contains the "[applied]" status column
  // plus the values of all columns referenced in the if-clause if the condition is not satisfied.
  std::unique_ptr<Schema> projection_;
  std::unique_ptr<YQLRowBlock> rowblock_;
};

class YQLReadOperation {
 public:
  explicit YQLReadOperation(const YQLReadRequestPB& request) : request_(request) {}

  CHECKED_STATUS Execute(
      const common::YQLStorageIf& yql_storage, const HybridTime& hybrid_time, const Schema& schema,
      YQLRowBlock* rowblock);

  const YQLResponsePB& response() const;

 private:
  const YQLReadRequestPB& request_;
  YQLResponsePB response_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_OPERATION_H_
