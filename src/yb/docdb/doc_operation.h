// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_OPERATION_H_
#define YB_DOCDB_DOC_OPERATION_H_

#include "rocksdb/db.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/primitive_value.h"
#include "yb/common/redis_protocol.pb.h"

namespace yb {
namespace docdb {

class DocWriteBatch;

class DocOperation {
 public:
  virtual ~DocOperation() {}

  virtual DocPath DocPathToLock() const = 0;
  virtual Status Apply(DocWriteBatch* doc_write_batch) = 0;
};

class YSQLWriteOperation: public DocOperation {
 public:
  YSQLWriteOperation(DocPath doc_path, PrimitiveValue value) : doc_path_(doc_path), value_(value) {
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

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_OPERATION_H_
