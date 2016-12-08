// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef YB_CLIENT_YB_OP_H_
#define YB_CLIENT_YB_OP_H_

#include <memory>
#include <string>

#include "yb/common/partial_row.h"
#include "yb/util/yb_export.h"

namespace yb {

class EncodedKey;

class RedisWriteRequestPB;
class RedisReadRequestPB;
class RedisResponsePB;

namespace client {

namespace internal {
class Batcher;
class AsyncRpc;
}  // namespace internal

class YBTable;

// A write or read operation operates on a single table and partial row.
// The YBOperation class itself allows the batcher to get to the
// generic information that it needs to process all write operations.
//
// On its own, the class does not represent any specific change and thus cannot
// be constructed independently.
//
// YBOperation also holds shared ownership of its YBTable to allow client's
// scope to end while the YBOperation is still alive.
class YB_EXPORT YBOperation {
 public:
  enum Type {
    INSERT = 1,
    UPDATE = 2,
    DELETE = 3,
    REDIS_WRITE = 4,
    REDIS_READ = 5,
  };
  virtual ~YBOperation();

  // See YBPartialRow API for field setters, etc.
  const YBPartialRow& row() const { return row_; }
  YBPartialRow* mutable_row() { return &row_; }

  virtual std::string ToString() const = 0;
  virtual Type type() const = 0;
  virtual bool read_only() = 0;
 protected:
  explicit YBOperation(const std::shared_ptr<YBTable>& table);

  std::shared_ptr<YBTable> const table_;
  YBPartialRow row_;

 private:
  friend class internal::Batcher;
  friend class internal::AsyncRpc;

  const YBTable* table() const { return table_.get(); }

  // Return the number of bytes required to buffer this operation,
  // including direct and indirect data.
  int64_t SizeInBuffer() const;

  DISALLOW_COPY_AND_ASSIGN(YBOperation);
};

// A single row insert to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being inserted
// An insert requires all key columns from the table schema to be defined.
class YB_EXPORT YBInsert : public YBOperation {
 public:
  virtual ~YBInsert();

  virtual std::string ToString() const OVERRIDE { return "INSERT " + row_.ToString(); }

  bool read_only() OVERRIDE { return false; };

 protected:
  virtual Type type() const OVERRIDE {
    return INSERT;
  }

 private:
  friend class YBTable;
  explicit YBInsert(const std::shared_ptr<YBTable>& table);
};

class YB_EXPORT YBRedisWriteOp : public YBOperation {
 public:
  virtual ~YBRedisWriteOp();

  const RedisWriteRequestPB& request() { return *redis_write_request_; }

  RedisWriteRequestPB* mutable_request() { return redis_write_request_.get(); }

  const RedisResponsePB& response() { return *redis_response_; }

  RedisResponsePB* mutable_response();

  virtual std::string ToString() const OVERRIDE;

  bool read_only() OVERRIDE { return false; };

 protected:
  virtual Type type() const OVERRIDE {
    return REDIS_WRITE;
  }

 private:
  friend class YBTable;
  explicit YBRedisWriteOp(const std::shared_ptr<YBTable>& table);
  std::unique_ptr<RedisWriteRequestPB> redis_write_request_;
  std::unique_ptr<RedisResponsePB> redis_response_;
};

class YB_EXPORT YBRedisReadOp : public YBOperation {
 public:
  virtual ~YBRedisReadOp();

  const RedisReadRequestPB& request() { return *redis_read_request_; }

  RedisReadRequestPB* mutable_request() { return redis_read_request_.get(); }

  bool has_response() { return redis_response_ ? true : false; }

  const RedisResponsePB& response() const;

  RedisResponsePB* mutable_response();

  virtual std::string ToString() const OVERRIDE;

  bool read_only() OVERRIDE { return true; };

 protected:
  virtual Type type() const OVERRIDE { return REDIS_READ; }

 private:
  friend class YBTable;
  explicit YBRedisReadOp(const std::shared_ptr<YBTable>& table);
  std::unique_ptr<RedisReadRequestPB> redis_read_request_;
  std::unique_ptr<RedisResponsePB> redis_response_;
};


// A single row update to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being updated.
// An update requires the key columns and at least one other column
// in the schema to be defined.
class YB_EXPORT YBUpdate : public YBOperation {
 public:
  virtual ~YBUpdate();

  virtual std::string ToString() const OVERRIDE { return "UPDATE " + row_.ToString(); }

  bool read_only() OVERRIDE { return false; };

 protected:
  virtual Type type() const OVERRIDE {
    return UPDATE;
  }

 private:
  friend class YBTable;
  explicit YBUpdate(const std::shared_ptr<YBTable>& table);
};


// A single row delete to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being deleted
// A delete requires just the key columns to be defined.
class YB_EXPORT YBDelete : public YBOperation {
 public:
  virtual ~YBDelete();

  virtual std::string ToString() const OVERRIDE { return "DELETE " + row_.ToString(); }

  bool read_only() OVERRIDE { return false; };

 protected:
  virtual Type type() const OVERRIDE {
    return DELETE;
  }

 private:
  friend class YBTable;
  explicit YBDelete(const std::shared_ptr<YBTable>& table);
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_YB_OP_H_
