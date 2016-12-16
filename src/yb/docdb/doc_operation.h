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

  virtual DocPath DocPathToLock() const = 0;
  virtual Status Apply(DocWriteBatch* doc_write_batch) = 0;
};

class KuduWriteOperation: public DocOperation {
 public:
  KuduWriteOperation(DocPath doc_path, PrimitiveValue value) : doc_path_(doc_path), value_(value) {
  }

  DocPath DocPathToLock() const override;

  Status Apply(DocWriteBatch *doc_write_batch) override;

 private:
  DocPath doc_path_;
  PrimitiveValue value_;
};

class RedisWriteOperation: public DocOperation {
 public:
  explicit RedisWriteOperation(yb::RedisWriteRequestPB request) : request_(request), response_() {}

  Status Apply(DocWriteBatch *doc_write_batch) override;

  DocPath DocPathToLock() const override;

  const RedisResponsePB &response();

 private:
  RedisWriteRequestPB request_;
  RedisResponsePB response_;
};

class RedisReadOperation {
 public:
  explicit RedisReadOperation(yb::RedisReadRequestPB request) : request_(request) {}

  Status Execute(rocksdb::DB *rocksdb, Timestamp timestamp);

  const RedisResponsePB &response();

 private:
  RedisReadRequestPB request_;
  RedisResponsePB response_;
};

class YSQLWriteOperation : public DocOperation {
 public:
  explicit YSQLWriteOperation(const YSQLWriteRequestPB& request);

  DocPath DocPathToLock() const override;

  Status Apply(DocWriteBatch* doc_write_batch) override;

  const YSQLResponsePB& response();

 private:
  const DocKey doc_key_;
  const DocPath doc_path_;
  const YSQLWriteRequestPB request_;
  YSQLResponsePB response_;
};

class YSQLReadOperation {
 public:
  explicit YSQLReadOperation(const YSQLReadRequestPB& request);

  Status Execute(rocksdb::DB *rocksdb, Timestamp timestamp, YSQLRowBlock* rowblock);

  const YSQLResponsePB& response();

 private:
  class YSQLRowReader;
  const YSQLReadRequestPB request_;
  YSQLResponsePB response_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_OPERATION_H_
