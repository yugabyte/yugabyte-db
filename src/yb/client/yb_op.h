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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_CLIENT_YB_OP_H_
#define YB_CLIENT_YB_OP_H_

#include <memory>
#include <string>

#include "yb/common/partial_row.h"
#include "yb/common/partition.h"

#include "yb/client/meta_cache.h"

namespace yb {

class EncodedKey;

class RedisWriteRequestPB;
class RedisReadRequestPB;
class RedisResponsePB;

class QLWriteRequestPB;
class QLReadRequestPB;
class QLResponsePB;

namespace client {

namespace internal {
class Batcher;
class AsyncRpc;
}  // namespace internal

class YBSession;
class YBStatusCallback;
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
class YBOperation {
 public:
  enum Type {
    INSERT = 1,
    UPDATE = 2,
    DELETE = 3,
    REDIS_WRITE = 4,
    REDIS_READ = 5,
    QL_WRITE = 6,
    QL_READ = 7,
  };
  virtual ~YBOperation();

  const YBTable* table() const { return table_.get(); }

  virtual std::string ToString() const = 0;
  virtual Type type() const = 0;
  virtual bool read_only() = 0;

  virtual void SetHashCode(uint16_t hash_code) = 0;

  const scoped_refptr<internal::RemoteTablet>& tablet() const {
    return tablet_;
  }

  void SetTablet(const scoped_refptr<internal::RemoteTablet>& tablet) {
    tablet_ = tablet;
  }

  // Returns the partition key of the operation.
  virtual CHECKED_STATUS GetPartitionKey(std::string* partition_key) const = 0;

 protected:
  explicit YBOperation(const std::shared_ptr<YBTable>& table);

  std::shared_ptr<YBTable> const table_;

 private:
  friend class internal::AsyncRpc;

  scoped_refptr<internal::RemoteTablet> tablet_;

  DISALLOW_COPY_AND_ASSIGN(YBOperation);
};

// Kudu operations are currently only used in some tests and should be removed once the dependent
// tests are migrated to YbQL operations.
class KuduOperation : public YBOperation {
 public:
  // See YBPartialRow API for field setters, etc.
  const YBPartialRow& row() const { return row_; }
  YBPartialRow* mutable_row() { return &row_; }
  virtual CHECKED_STATUS GetPartitionKey(std::string* partition_key) const;

 protected:
  explicit KuduOperation(const std::shared_ptr<YBTable>& table);
  YBPartialRow row_;

 private:
  friend class internal::Batcher;

  // Return the number of bytes required to buffer this operation,
  // including direct and indirect data.
  int64_t SizeInBuffer() const;
};

// Kudu inserts are currently only used in some tests and should be removed once the dependent
// tests are migrated to YbQL operations.
//
// A single row insert to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being inserted
// An insert requires all key columns from the table schema to be defined.
class KuduInsert : public KuduOperation {
 public:
  virtual ~KuduInsert();

  virtual std::string ToString() const override { return "INSERT " + row_.ToString(); }

  virtual bool read_only() override { return false; };

  // Note: SetHashCode only needed for Redis and YBQL operations. The empty method will be gone
  // when KuduInsert / KuduUpdate / KuduDelete are deprecated.
  void SetHashCode(uint16_t hash_code) override {};

 protected:
  virtual Type type() const override {
    return INSERT;
  }

 private:
  friend class YBTable;
  explicit KuduInsert(const std::shared_ptr<YBTable>& table);
};

// Kudu updates are currently only used in some tests and should be removed once the dependent
// tests are migrated to use YbQL operations.
//
// A single row update to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being updated.
// An update requires the key columns and at least one other column
// in the schema to be defined.
class KuduUpdate : public KuduOperation {
 public:
  virtual ~KuduUpdate();

  virtual std::string ToString() const override { return "UPDATE " + row_.ToString(); }

  virtual bool read_only() override { return false; };

  // Note: SetHashCode only needed for Redis and YBQL operations. The empty method will be gone
  // when KuduInsert / KuduUpdate / KuduDelete are deprecated.
  void SetHashCode(uint16_t hash_code) override {};

 protected:
  virtual Type type() const override {
    return UPDATE;
  }

 private:
  friend class YBTable;
  explicit KuduUpdate(const std::shared_ptr<YBTable>& table);
};

// Kudu deletes are currently only used in some tests and should be removed once the dependent
// tests are migrated to use YbQL operations.
//
// A single row delete to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being deleted
// A delete requires just the key columns to be defined.
class KuduDelete : public KuduOperation {
 public:
  virtual ~KuduDelete();

  virtual std::string ToString() const override { return "DELETE " + row_.ToString(); }

  virtual bool read_only() override { return false; };

  // Note: SetHashCode only needed for Redis and YBQL operations. The empty method will be gone
  // when KuduInsert / KuduUpdate / KuduDelete are deprecated.
  void SetHashCode(uint16_t hash_code) override {};

 protected:
  virtual Type type() const override {
    return DELETE;
  }

 private:
  friend class YBTable;
  explicit KuduDelete(const std::shared_ptr<YBTable>& table);
};

class YBRedisOp : public YBOperation {
 public:
  explicit YBRedisOp(const std::shared_ptr<YBTable>& table);
  virtual ~YBRedisOp();

  bool has_response() { return redis_response_ ? true : false; }

  const RedisResponsePB& response() const;

  RedisResponsePB* mutable_response();

  virtual const std::string& GetKey() const = 0;

 private:
  std::unique_ptr<RedisResponsePB> redis_response_;
};

class YBRedisWriteOp : public YBRedisOp {
 public:
  explicit YBRedisWriteOp(const std::shared_ptr<YBTable>& table);
  virtual ~YBRedisWriteOp();

  // Note: to avoid memory copy, this RedisWriteRequestPB is moved into tserver WriteRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see WriteRpc's constructor).
  const RedisWriteRequestPB& request() const { return *redis_write_request_; }

  RedisWriteRequestPB* mutable_request() { return redis_write_request_.get(); }

  virtual std::string ToString() const override;

  virtual bool read_only() override { return false; };

  // Set the hash key in the WriteRequestPB.
  void SetHashCode(uint16_t hash_code) override;

  virtual const std::string& GetKey() const override;

  virtual CHECKED_STATUS GetPartitionKey(std::string* partition_key) const override;

 protected:
  virtual Type type() const override {
    return REDIS_WRITE;
  }

 private:
  friend class YBTable;
  std::unique_ptr<RedisWriteRequestPB> redis_write_request_;
  std::unique_ptr<RedisResponsePB> redis_response_;
};


class YBRedisReadOp : public YBRedisOp {
 public:
  explicit YBRedisReadOp(const std::shared_ptr<YBTable>& table);
  virtual ~YBRedisReadOp();

  // Note: to avoid memory copy, this RedisReadRequestPB is moved into tserver ReadRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see ReadRpc's constructor).
  const RedisReadRequestPB& request() const { return *redis_read_request_; }

  RedisReadRequestPB* mutable_request() { return redis_read_request_.get(); }

  virtual std::string ToString() const override;

  virtual bool read_only() override { return true; };

  // Set the hash key in the ReadRequestPB.
  void SetHashCode(uint16_t hash_code) override;

  virtual const std::string& GetKey() const override;

  virtual CHECKED_STATUS GetPartitionKey(std::string* partition_key) const override;

 protected:
  virtual Type type() const override { return REDIS_READ; }

 private:
  friend class YBTable;
  std::unique_ptr<RedisReadRequestPB> redis_read_request_;
};

class YBqlOp : public YBOperation {
 public:
  virtual ~YBqlOp();

  const QLResponsePB& response() const { return *ql_response_; }

  QLResponsePB* mutable_response() { return ql_response_.get(); }

  std::string&& rows_data() { return std::move(rows_data_); }

  std::string* mutable_rows_data() { return &rows_data_; }

  // Set the hash key in the partial row of this QL operation.
  virtual void SetHashCode(uint16_t hash_code) override = 0;

 protected:
  explicit YBqlOp(const std::shared_ptr<YBTable>& table);
  std::unique_ptr<QLResponsePB> ql_response_;
  std::string rows_data_;
};

class YBqlWriteOp : public YBqlOp {
 public:
  explicit YBqlWriteOp(const std::shared_ptr<YBTable>& table);
  virtual ~YBqlWriteOp();

  // Note: to avoid memory copy, this QLWriteRequestPB is moved into tserver WriteRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see WriteRpc's constructor).
  const QLWriteRequestPB& request() const { return *ql_write_request_; }

  QLWriteRequestPB* mutable_request() { return ql_write_request_.get(); }

  std::string ToString() const override;

  bool read_only() override { return false; };

  virtual void SetHashCode(uint16_t hash_code) override;

  virtual CHECKED_STATUS GetPartitionKey(std::string* partition_key) const override;

 protected:
  virtual Type type() const override {
    return QL_WRITE;
  }

 private:
  friend class YBTable;
  static YBqlWriteOp *NewInsert(const std::shared_ptr<YBTable>& table);
  static YBqlWriteOp *NewUpdate(const std::shared_ptr<YBTable>& table);
  static YBqlWriteOp *NewDelete(const std::shared_ptr<YBTable>& table);
  std::unique_ptr<QLWriteRequestPB> ql_write_request_;
};

class YBqlReadOp : public YBqlOp {
 public:
  virtual ~YBqlReadOp();

  static YBqlReadOp *NewSelect(const std::shared_ptr<YBTable>& table);

  // Note: to avoid memory copy, this QLReadRequestPB is moved into tserver ReadRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see ReadRpc's constructor).
  const QLReadRequestPB& request() const { return *ql_read_request_; }

  QLReadRequestPB* mutable_request() { return ql_read_request_.get(); }

  virtual std::string ToString() const override;

  virtual bool read_only() override { return true; };

  virtual void SetHashCode(uint16_t hash_code) override;

  // Returns the partition key of the read request if it exists.
  // Also sets the hash_code and max_hash_code in the request.
  virtual CHECKED_STATUS GetPartitionKey(std::string* partition_key) const override;

  const YBConsistencyLevel yb_consistency_level() {
    return yb_consistency_level_;
  }

  void set_yb_consistency_level(const YBConsistencyLevel yb_consistency_level) {
    yb_consistency_level_ = yb_consistency_level;
  }

 protected:
  virtual Type type() const override { return QL_READ; }

 private:
  friend class YBTable;
  explicit YBqlReadOp(const std::shared_ptr<YBTable>& table);
  std::unique_ptr<QLReadRequestPB> ql_read_request_;
  YBConsistencyLevel yb_consistency_level_;
};


}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_YB_OP_H_
