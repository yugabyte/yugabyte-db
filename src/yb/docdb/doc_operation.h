// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_OPERATION_H_
#define YB_DOCDB_DOC_OPERATION_H_

#include "rocksdb/db.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/primitive_value.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/ysql_protocol.pb.h"
#include "yb/common/ysql_rowblock.h"

namespace yb {
namespace docdb {

class DocWriteBatch;

class DocOperation {
 public:
  virtual ~DocOperation() {}

  // Does the operation require a read snapshot to be taken before being applied? If so, a
  // clean snapshot timestamp will be supplied when Apply() is called. For example,
  // YSQLWriteOperation for a DML with a "... IF <condition> ..." clause needs to read the row to
  // evaluate the condition before the write and needs a read snapshot for a consistent read.
  virtual bool RequireReadSnapshot() const = 0;
  virtual DocPath DocPathToLock() const = 0;
  virtual CHECKED_STATUS Apply(
      DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const Timestamp& timestamp) = 0;
};

class KuduWriteOperation: public DocOperation {
 public:
  KuduWriteOperation(DocPath doc_path, PrimitiveValue value) : doc_path_(doc_path), value_(value) {
  }

  bool RequireReadSnapshot() const override { return false; }

  DocPath DocPathToLock() const override;

  CHECKED_STATUS Apply(
      DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const Timestamp& timestamp) override;

 private:
  DocPath doc_path_;
  PrimitiveValue value_;
};

class RedisWriteOperation: public DocOperation {
 public:
  explicit RedisWriteOperation(yb::RedisWriteRequestPB request) : request_(request), response_() {}

  bool RequireReadSnapshot() const override { return false; }

  CHECKED_STATUS Apply(
      DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const Timestamp& timestamp) override;

  DocPath DocPathToLock() const override;

  const RedisResponsePB &response();

 private:
  RedisWriteRequestPB request_;
  RedisResponsePB response_;
};

class RedisReadOperation {
 public:
  explicit RedisReadOperation(yb::RedisReadRequestPB request) : request_(request) {}

  CHECKED_STATUS Execute(rocksdb::DB *rocksdb, const Timestamp& timestamp);

  const RedisResponsePB &response();

 private:
  RedisReadRequestPB request_;
  RedisResponsePB response_;
};

class YSQLWriteOperation : public DocOperation {
 public:
  YSQLWriteOperation(
      const YSQLWriteRequestPB& request, const Schema& schema, YSQLResponsePB* response);

  bool RequireReadSnapshot() const override;

  DocPath DocPathToLock() const override;

  CHECKED_STATUS Apply(
      DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const Timestamp& timestamp) override;

  const YSQLWriteRequestPB& request() const { return request_; }
  YSQLResponsePB* response() const { return response_; }

  // Rowblock to return the "[applied]" status for conditional DML.
  const YSQLRowBlock* rowblock() const { return rowblock_.get(); }

 private:
  CHECKED_STATUS IsConditionSatisfied(
      const YSQLConditionPB& condition, rocksdb::DB *rocksdb, const Timestamp& timestamp,
      bool* should_apply, std::unique_ptr<YSQLRowBlock>* rowblock);

  const Schema& schema_;
  const DocKey doc_key_;
  const DocPath doc_path_;
  const YSQLWriteRequestPB request_;
  YSQLResponsePB* response_;
  // The row and the column schema that is returned to the CQL client for an INSERT/UPDATE/DELETE
  // that has a "... IF <condition> ..." clause. The row contains the "[applied]" status column
  // plus the values of all columns referenced in the if-clause if the condition is not satisfied.
  std::unique_ptr<Schema> projection_;
  std::unique_ptr<YSQLRowBlock> rowblock_;
};

class YSQLReadOperation {
 public:
  explicit YSQLReadOperation(const YSQLReadRequestPB& request);

  CHECKED_STATUS Execute(
      rocksdb::DB *rocksdb, const Timestamp& timestamp, const Schema& schema,
      YSQLRowBlock* rowblock);

  const YSQLResponsePB& response() const;

 private:
  const YSQLReadRequestPB request_;
  YSQLResponsePB response_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_OPERATION_H_
