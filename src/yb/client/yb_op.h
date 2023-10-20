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
#pragma once

#include <memory>
#include <string>

#include <boost/optional.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/dockv/partial_row.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/retryable_request.h"
#include "yb/common/transaction.pb.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/ref_cnt_buffer.h"

namespace yb {
namespace client {

namespace internal {
class Batcher;
class AsyncRpc;
class RemoteTablet;
}  // namespace internal

class YBSession;
class YBStatusCallback;
class YBTable;

YB_DEFINE_ENUM(OpGroup, (kWrite)(kLeaderRead)(kConsistentPrefixRead));

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
    // Redis opcodes.
    REDIS_WRITE = 4,
    REDIS_READ = 5,

    // CQL opcodes.
    QL_WRITE = 6,
    QL_READ = 7,

    // Postgresql opcodes.
    PGSQL_WRITE = 8,
    PGSQL_READ = 9,
  };
  virtual ~YBOperation();

  std::shared_ptr<const YBTable> table() const { return table_; }
  std::shared_ptr<YBTable> mutable_table() const { return table_; }

  void ResetTable(std::shared_ptr<YBTable> new_table);

  virtual std::string ToString() const = 0;
  virtual Type type() const = 0;
  virtual bool read_only() const = 0;
  virtual bool succeeded() const = 0;
  virtual bool returns_sidecar() = 0;

  virtual OpGroup group() {
    return read_only() ? OpGroup::kLeaderRead : OpGroup::kWrite;
  }

  virtual bool applied() {
    return succeeded();
  }

  virtual bool should_apply_intents(IsolationLevel isolation_level) {
    return !read_only() || isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION;
  }

  virtual void SetHashCode(uint16_t hash_code) = 0;

  const scoped_refptr<internal::RemoteTablet>& tablet() const {
    return tablet_;
  }

  void SetTablet(const scoped_refptr<internal::RemoteTablet>& tablet);

  // Resets tablet, so it will be re-resolved on applying this operation.
  void ResetTablet();

  std::optional<RetryableRequestId> request_id() const {
    return request_id_;
  }

  void set_request_id(RetryableRequestId id) {
    request_id_ = id;
  }

  void reset_request_id() {
    request_id_.reset();
  }

  // Returns the partition key of the operation.
  virtual Status GetPartitionKey(std::string* partition_key) const = 0;

  // Whether this is an operation on one of the YSQL system catalog tables.
  bool IsYsqlCatalogOp() const;

  // Mark table this op is designated for as having stale partitions.
  void MarkTablePartitionListAsStale();

  // If partition_list_version is set YBSession guarantees that this operation instance won't
  // be applied to the tablet with a different table partition_list_version (meaning serving
  // different range of partition keys). If versions do not match YBSession will report
  // ClientError::kTablePartitionListVersionDoesNotMatch.
  // If partition_list_version is not set - no such check will be performed.
  void SetPartitionListVersion(PartitionListVersion partition_list_version) {
    partition_list_version_ = partition_list_version;
  }

  boost::optional<PartitionListVersion> partition_list_version() const {
    return partition_list_version_;
  }

  int64_t GetQueryId() const {
    return reinterpret_cast<int64_t>(this);
  }

 protected:
  explicit YBOperation(const std::shared_ptr<YBTable>& table);

  std::shared_ptr<YBTable> table_;

 private:
  friend class internal::AsyncRpc;

  scoped_refptr<internal::RemoteTablet> tablet_;

  boost::optional<PartitionListVersion> partition_list_version_;

  // Persist retryable request ID across internal retries within the same YBSession
  // to prevent duplicate writes due to internal retries.
  std::optional<RetryableRequestId> request_id_;

  DISALLOW_COPY_AND_ASSIGN(YBOperation);
};

//--------------------------------------------------------------------------------------------------
// YBRedis Operators.
//--------------------------------------------------------------------------------------------------

class YBRedisOp : public YBOperation {
 public:
  explicit YBRedisOp(const std::shared_ptr<YBTable>& table);

  bool has_response() { return redis_response_ ? true : false; }
  virtual size_t space_used_by_request() const = 0;

  const RedisResponsePB& response() const;

  RedisResponsePB* mutable_response();

  uint16_t hash_code() const { return hash_code_; }

  // Redis does not use sidecars.
  bool returns_sidecar() override { return false; }

  virtual const std::string& GetKey() const = 0;

 protected:
  uint16_t hash_code_ = 0;
  std::unique_ptr<RedisResponsePB> redis_response_;
};

class YBRedisWriteOp : public YBRedisOp {
 public:
  explicit YBRedisWriteOp(const std::shared_ptr<YBTable>& table);

  // Note: to avoid memory copy, this RedisWriteRequestPB is moved into tserver WriteRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see WriteRpc's constructor).
  const RedisWriteRequestPB& request() const { return *redis_write_request_; }
  size_t space_used_by_request() const override;

  RedisWriteRequestPB* mutable_request() { return redis_write_request_.get(); }

  std::string ToString() const override;

  bool read_only() const override { return false; }
  bool succeeded() const override { return false; } // TODO(dtxn) implement

  // Set the hash key in the WriteRequestPB.
  void SetHashCode(uint16_t hash_code) override;

  const std::string& GetKey() const override;

  Status GetPartitionKey(std::string* partition_key) const override;

 protected:
  virtual Type type() const override { return REDIS_WRITE; }

 private:
  friend class YBTable;
  std::unique_ptr<RedisWriteRequestPB> redis_write_request_;
  std::unique_ptr<RedisResponsePB> redis_response_;
};


class YBRedisReadOp : public YBRedisOp {
 public:
  explicit YBRedisReadOp(const std::shared_ptr<YBTable>& table);

  // Note: to avoid memory copy, this RedisReadRequestPB is moved into tserver ReadRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see ReadRpc's constructor).
  const RedisReadRequestPB& request() const { return *redis_read_request_; }
  size_t space_used_by_request() const override;

  RedisReadRequestPB* mutable_request() { return redis_read_request_.get(); }

  std::string ToString() const override;

  bool read_only() const override { return true; }
  bool succeeded() const override { return false; } // TODO(dtxn) implement

  // Set the hash key in the ReadRequestPB.
  void SetHashCode(uint16_t hash_code) override;

  const std::string& GetKey() const override;

  Status GetPartitionKey(std::string* partition_key) const override;

 protected:
  Type type() const override { return REDIS_READ; }
  OpGroup group() override;

 private:
  friend class YBTable;
  std::unique_ptr<RedisReadRequestPB> redis_read_request_;
};

//--------------------------------------------------------------------------------------------------
// YBCql Operators.
//--------------------------------------------------------------------------------------------------

class YBqlOp : public YBOperation {
 public:
  ~YBqlOp();

  const QLResponsePB& response() const { return *ql_response_; }

  QLResponsePB* mutable_response() { return ql_response_.get(); }

  const RefCntSlice& rows_data() { return rows_data_; }

  void set_rows_data(const RefCntSlice& value) {
    rows_data_ = value;
  }

  bool succeeded() const override;

 protected:
  explicit YBqlOp(const std::shared_ptr<YBTable>& table);
  std::unique_ptr<QLResponsePB> ql_response_;
  RefCntSlice rows_data_;
};

class YBqlWriteOp : public YBqlOp {
 public:
  explicit YBqlWriteOp(const std::shared_ptr<YBTable>& table);
  ~YBqlWriteOp();

  // Note: to avoid memory copy, this QLWriteRequestPB is moved into tserver WriteRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see WriteRpc's constructor).
  const QLWriteRequestPB& request() const { return *ql_write_request_; }

  QLWriteRequestPB* mutable_request() { return ql_write_request_.get(); }

  std::string ToString() const override;

  bool read_only() const override { return false; };

  bool returns_sidecar() override;

  void SetHashCode(uint16_t hash_code) override;

  uint16_t GetHashCode() const;

  Status GetPartitionKey(std::string* partition_key) const override;

  // Does this operation read/write the static or primary row?
  bool ReadsStaticRow() const;
  bool ReadsPrimaryRow() const;
  bool WritesStaticRow() const;
  bool WritesPrimaryRow() const;

  void set_writes_static_row(const bool value) { writes_static_row_ = value; }
  void set_writes_primary_row(const bool value) { writes_primary_row_ = value; }

  void set_write_time_for_backfill(HybridTime value) {
    write_time_for_backfill_ = value;
  }

  HybridTime write_time_for_backfill() const {
    return write_time_for_backfill_;
  }

 protected:
  Type type() const override { return QL_WRITE; }

 private:
  friend class YBTable;
  static std::unique_ptr<YBqlWriteOp> NewInsert(const std::shared_ptr<YBTable>& table);
  static std::unique_ptr<YBqlWriteOp> NewUpdate(const std::shared_ptr<YBTable>& table);
  static std::unique_ptr<YBqlWriteOp> NewDelete(const std::shared_ptr<YBTable>& table);
  std::unique_ptr<QLWriteRequestPB> ql_write_request_;

  // Does this operation write to the static or primary row?
  bool writes_static_row_ = false;
  bool writes_primary_row_ = false;
  HybridTime write_time_for_backfill_;
};

// Hash and equal functions to define a set of write operations that do not overlap by their
// hash (or primary) keys.
struct YBqlWriteHashKeyComparator {
  size_t operator() (const YBqlWriteOpPtr& op) const;
  bool operator() (const YBqlWriteOpPtr& op1, const YBqlWriteOpPtr& op2) const;
};

struct YBqlWritePrimaryKeyComparator {
  size_t operator() (const YBqlWriteOpPtr& op) const;
  bool operator() (const YBqlWriteOpPtr& op1, const YBqlWriteOpPtr& op2) const;
};

class YBqlReadOp : public YBqlOp {
 public:
  ~YBqlReadOp();

  static std::unique_ptr<YBqlReadOp> NewSelect(const std::shared_ptr<YBTable>& table);

  // Note: to avoid memory copy, this QLReadRequestPB is moved into tserver ReadRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see ReadRpc's constructor).
  const QLReadRequestPB& request() const { return *ql_read_request_; }

  QLReadRequestPB* mutable_request() { return ql_read_request_.get(); }

  std::string ToString() const override;

  bool read_only() const override { return true; };

  bool returns_sidecar() override { return true; }

  void SetHashCode(uint16_t hash_code) override;

  // Returns the partition key of the read request if it exists.
  // Also sets the hash_code and max_hash_code in the request.
  Status GetPartitionKey(std::string* partition_key) const override;

  YBConsistencyLevel yb_consistency_level() {
    return yb_consistency_level_;
  }

  void set_yb_consistency_level(const YBConsistencyLevel yb_consistency_level) {
    yb_consistency_level_ = yb_consistency_level;
  }

  std::vector<ColumnSchema> MakeColumnSchemasFromRequest() const;
  Result<qlexpr::QLRowBlock> MakeRowBlock() const;

  const ReadHybridTime& read_time() const { return read_time_; }
  void SetReadTime(const ReadHybridTime& value) { read_time_ = value; }

 protected:
  Type type() const override { return QL_READ; }
  OpGroup group() override;

 private:
  friend class YBTable;
  explicit YBqlReadOp(const std::shared_ptr<YBTable>& table);
  std::unique_ptr<QLReadRequestPB> ql_read_request_;
  YBConsistencyLevel yb_consistency_level_;
  ReadHybridTime read_time_;
};

std::vector<ColumnSchema> MakeColumnSchemasFromColDesc(
  const google::protobuf::RepeatedPtrField<QLRSColDescPB>& rscol_descs);

//--------------------------------------------------------------------------------------------------
// YB Postgresql Operators.
//--------------------------------------------------------------------------------------------------

class YBPgsqlOp : public YBOperation {
 public:
  YBPgsqlOp(
      const std::shared_ptr<YBTable>& table, rpc::Sidecars* sidecars);
  ~YBPgsqlOp();

  const PgsqlResponsePB& response() const { return *response_; }

  PgsqlResponsePB* mutable_response() { return response_.get(); }

  bool succeeded() const override;

  bool applied() override;

  void SetSidecarIndex(size_t idx) {
    sidecar_index_ = idx;
  }

  bool has_sidecar() const {
    return sidecar_index_ != -1;
  }

  size_t sidecar_index() const {
    return sidecar_index_;
  }

  rpc::Sidecars& sidecars() const {
    return sidecars_;
  }

 protected:
  std::unique_ptr<PgsqlResponsePB> response_;
  int64_t sidecar_index_ = -1;
  rpc::Sidecars& sidecars_;
};

class YBPgsqlWriteOp : public YBPgsqlOp {
 public:
  YBPgsqlWriteOp(
      const std::shared_ptr<YBTable>& table, rpc::Sidecars* sidecars,
      PgsqlWriteRequestPB* request = nullptr);
  ~YBPgsqlWriteOp();

  // Note: to avoid memory copy, this PgsqlWriteRequestPB is moved into tserver WriteRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see WriteRpc's constructor).
  const PgsqlWriteRequestPB& request() const { return *request_; }

  PgsqlWriteRequestPB* mutable_request() { return request_; }

  std::string ToString() const override;

  bool read_only() const override { return false; };

  // TODO check for e.g. returning clause.
  bool returns_sidecar() override { return true; }

  void SetHashCode(uint16_t hash_code) override;

  void set_is_single_row_txn(bool is_single_row_txn) {
    is_single_row_txn_ = is_single_row_txn;
  }

  const HybridTime& write_time() const { return write_time_; }
  void SetWriteTime(const HybridTime& value) { write_time_ = value; }

  Status GetPartitionKey(std::string* partition_key) const override;

  static YBPgsqlWriteOpPtr NewInsert(const YBTablePtr& table, rpc::Sidecars* sidecars);
  static YBPgsqlWriteOpPtr NewUpdate(const YBTablePtr& table, rpc::Sidecars* sidecars);
  static YBPgsqlWriteOpPtr NewDelete(const YBTablePtr& table, rpc::Sidecars* sidecars);
  static YBPgsqlWriteOpPtr NewFetchSequence(const YBTablePtr& table, rpc::Sidecars* sidecars);

 protected:
  virtual Type type() const override { return PGSQL_WRITE; }

 private:
  friend class YBTable;

  PgsqlWriteRequestPB* request_;
  std::unique_ptr<PgsqlWriteRequestPB> request_holder_;
  // Whether this operation should be run as a single row txn.
  // Else could be distributed transaction (or non-transactional) depending on target table type.
  bool is_single_row_txn_ = false;
  HybridTime write_time_;
};

class YBPgsqlReadOp : public YBPgsqlOp {
 public:
  YBPgsqlReadOp(
      const std::shared_ptr<YBTable>& table, rpc::Sidecars* sidecars,
      PgsqlReadRequestPB* request = nullptr);

  static YBPgsqlReadOpPtr NewSelect(
      const std::shared_ptr<YBTable>& table, rpc::Sidecars* sidecars);

  // Note: to avoid memory copy, this PgsqlReadRequestPB is moved into tserver ReadRequestPB
  // when the request is sent to tserver. It is restored after response is received from tserver
  // (see ReadRpc's constructor).
  const PgsqlReadRequestPB& request() const { return *request_; }

  PgsqlReadRequestPB* mutable_request() { return request_; }

  std::string ToString() const override;

  bool read_only() const override { return true; };

  bool returns_sidecar() override { return true; }

  void SetHashCode(uint16_t hash_code) override;

  YBConsistencyLevel yb_consistency_level() {
    return yb_consistency_level_;
  }

  void set_yb_consistency_level(const YBConsistencyLevel yb_consistency_level) {
    yb_consistency_level_ = yb_consistency_level;
  }

  std::vector<ColumnSchema> MakeColumnSchemasFromRequest() const;

  static std::vector<ColumnSchema> MakeColumnSchemasFromColDesc(
      const google::protobuf::RepeatedPtrField<PgsqlRSColDescPB>& rscol_descs);

  bool should_apply_intents(IsolationLevel isolation_level) override;
  void SetUsedReadTime(const ReadHybridTime& used_time, const TabletId& tablet);
  const ReadHybridTime& used_read_time() const { return used_read_time_; }
  const TabletId& used_tablet() const { return used_tablet_; }

  Status GetPartitionKey(std::string* partition_key) const override;

 protected:
  virtual Type type() const override { return PGSQL_READ; }
  OpGroup group() override;

 private:
  friend class YBTable;

  PgsqlReadRequestPB* request_;
  std::unique_ptr<PgsqlReadRequestPB> request_holder_;
  YBConsistencyLevel yb_consistency_level_ = YBConsistencyLevel::STRONG;
  ReadHybridTime used_read_time_;
  // The tablet that served this operation.
  TabletId used_tablet_;
};

// This class is not thread-safe, though different YBNoOp objects on
// different threads may share a single YBTable object.
class YBNoOp {
 public:
  // Initialize the NoOp request object. The given 'table' object must remain valid
  // for the lifetime of this object.
  explicit YBNoOp(const std::shared_ptr<YBTable>& table);

  // Executes a no-op request against the tablet server on which the row specified
  // by "key" lives.
  Status Execute(YBClient* client, const dockv::YBPartialRow& key);
 private:
  const std::shared_ptr<YBTable> table_;

  DISALLOW_COPY_AND_ASSIGN(YBNoOp);
};

Status InitPartitionKey(
    const Schema& schema, const dockv::PartitionSchema& partition_schema,
    const TablePartitionList& partitions, LWPgsqlReadRequestPB* request);

Status InitPartitionKey(
    const Schema& schema, const dockv::PartitionSchema& partition_schema,
    LWPgsqlWriteRequestPB* request);

Status GetRangePartitionBounds(
    const Schema& schema,
    const PgsqlReadRequestPB& request,
    dockv::KeyEntryValues* lower_bound,
    dockv::KeyEntryValues* upper_bound);

Status GetRangePartitionBounds(
    const Schema& schema,
    const LWPgsqlReadRequestPB& request,
    dockv::KeyEntryValues* lower_bound,
    dockv::KeyEntryValues* upper_bound);

bool IsTolerantToPartitionsChange(const YBOperation& op);

Result<const PartitionKey&> TEST_FindPartitionKeyByUpperBound(
    const TablePartitionList& partitions, const PgsqlReadRequestPB& request);

}  // namespace client
}  // namespace yb
