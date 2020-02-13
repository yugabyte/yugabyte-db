//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_DOC_OP_H_
#define YB_YQL_PGGATE_PG_DOC_OP_H_

#include <deque>

#include "yb/util/locks.h"
#include "yb/client/yb_op.h"
#include "yb/yql/pggate/pg_session.h"

namespace yb {
namespace pggate {

YB_STRONGLY_TYPED_BOOL(RequestSent);

class PgDocOp : public std::enable_shared_from_this<PgDocOp> {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocOp> SharedPtr;
  typedef std::shared_ptr<const PgDocOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocOp> UniPtr;
  typedef std::unique_ptr<const PgDocOp> UniPtrConst;

  // Constructors & Destructors.
  explicit PgDocOp(const PgSession::ScopedRefPtr& pg_session);
  virtual ~PgDocOp();

  // Set execution control parameters.
  // When "exec_params" is null, the default setup in PgExecParameters are used.
  void SetExecParams(const PgExecParameters& exec_params);

  // Execute the op. Return true if the request has been sent and is awaiting the result.
  virtual Result<RequestSent> Execute(bool force_non_bufferable = false);

  // Get the result of the op. Empty string is used to indicate end of data.
  virtual Result<string> GetResult();

  Result<int32_t> GetRowsAffectedCount() const;

 protected:
  virtual void Init();

  // Session control.
  PgSession::ScopedRefPtr pg_session_;

  // Operation time. This time is set at the start and must stay the same for the lifetime of the
  // operation to ensure that it is operating on one snapshot.
  uint64_t read_time_ = 0;

  // Future object to fetch a response from DocDB after sending a request.
  // Object's valid() method returns false in case no request is sent
  // or sent request was buffered by the session.
  // Only one request can be sent to DocDB at a time.
  PgSessionAsyncRunResult response_;

  // Whether all requested data by the statement has been received or there's a run-time error.
  bool end_of_data_ = false;

  // Exec control parameters.
  PgExecParameters exec_params_;

  // Caching state variables.
  std::deque<string> result_cache_;

 private:
  CHECKED_STATUS SendRequest(bool force_non_bufferable);
  CHECKED_STATUS ProcessResponse(const Status& exec_status);
  virtual CHECKED_STATUS SendRequestImpl(bool force_non_bufferable) = 0;
  virtual CHECKED_STATUS ProcessResponseImpl(const Status& exec_status) = 0;
  virtual int32_t GetRowsAffectedCountImpl() const { return 0; }

  // Result set either from selected or returned targets is cached in a list of strings.
  // Querying state variables.
  Status exec_status_ = Status::OK();
};

class PgDocReadOp : public PgDocOp {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocReadOp> SharedPtr;
  typedef std::shared_ptr<const PgDocReadOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocReadOp> UniPtr;
  typedef std::unique_ptr<const PgDocReadOp> UniPtrConst;

  // Constructors & Destructors.
  PgDocReadOp(const PgSession::ScopedRefPtr& pg_session,
              size_t num_hash_key_columns,
              std::unique_ptr<client::YBPgsqlReadOp> read_op);

 private:
  // Process response from DocDB.
  void Init() override;
  CHECKED_STATUS SendRequestImpl(bool force_non_bufferable) override;
  CHECKED_STATUS ProcessResponseImpl(const Status& exec_status) override;

  // Analyze options and pick the appropriate prefetch limit.
  void SetRequestPrefetchLimit();

  // Set the row_mark_type field of our read request based on our exec control parameter.
  void SetRowMark();

  const size_t num_hash_key_columns_;

  // Template operation, used to fill in read_ops_ by copying it with custom partition_column_values
  std::unique_ptr<client::YBPgsqlReadOp> template_op_;

  // Initialize up to N new operations from template_op_ and add them to read_ops_.
  //
  // If partition column binds are defined, partition_column_values on each opearation
  // is set to be the next permutation. Otherwise, template_op_ is copied as-is just once.
  //
  // Also updates the value of can_produce_more_ops_.
  void InitializeNextOps(int num_ops);

  // Used internally for InitializeNextOps to keep track of which permutation should be used
  // to construct the next read_op.
  // Is valid as long as can_produce_more_ops_ is true.
  //
  // Example:
  // For a query clause "h1 = 1 AND h2 IN (2,3) AND h3 IN (4,5,6) AND h4 = 7",
  // there are 1*2*3*1 = 6 possible permutation.
  // As such, this field will take on values 0 through 5.
  int next_op_idx_ = 0;

  // Used internally for InitializeNextOps to holds all partition expressions of expressions.
  // Elements correspond to a hash columns, in the same order as they were defined
  // in CREATE TABLE statement.
  // This is somewhat similar to what hash_values_options_ in CQL is used for.
  //
  // Example:
  // For a query clause "h1 = 1 AND h2 IN (2,3) AND h3 IN (4,5,6) AND h4 = 7",
  // this will be initialized to [[1], [2, 3], [4, 5, 6], [7]]
  std::vector<std::vector<const PgsqlExpressionPB*>> partition_exprs_;

  // True when either:
  // 1) Partition columns are not bound but the request hasn't been sent yet.
  // 2) Partition columns are bound and some permutations remain unprocessed.
  bool can_produce_more_ops_ = true;

  // Operation(s).
  //
  // If there's more than one, partition_column_values will be fully specified on all of them.
  //
  // This list is initialized only in SendRequestUnlocked - i.e. just before operations are sent.
  // In ReceiveResponse this list is pruned from exhausted operations, remaining are set up to
  // retrieve the next batch of data.
  std::vector<std::shared_ptr<client::YBPgsqlReadOp>> read_ops_;
};

class PgDocWriteOp : public PgDocOp {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocWriteOp> SharedPtr;
  typedef std::shared_ptr<const PgDocWriteOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocWriteOp> UniPtr;
  typedef std::unique_ptr<const PgDocWriteOp> UniPtrConst;

  // Constructors & Destructors.
  PgDocWriteOp(const PgSession::ScopedRefPtr& pg_session,
               const PgObjectId& relation_id,
               std::unique_ptr<client::YBPgsqlWriteOp> write_op);

 private:
  CHECKED_STATUS SendRequestImpl(bool force_non_bufferable) override;
  CHECKED_STATUS ProcessResponseImpl(const Status& exec_status) override;
  int32_t GetRowsAffectedCountImpl() const override { return rows_affected_count_; }

  std::shared_ptr<client::YBPgsqlWriteOp> write_op_;
  PgObjectId relation_id_;
  int32_t rows_affected_count_ = 0;
};

// TODO(neil)
// - Compound operator execute multiple singular ops in one transaction.
// - All virtual functions should be redefined for this class because default behaviors are for
//   singular operators.
class PgDocCompoundOp : public PgDocOp {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocCompoundOp> SharedPtr;
  typedef std::shared_ptr<const PgDocCompoundOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocCompoundOp> UniPtr;
  typedef std::unique_ptr<const PgDocCompoundOp> UniPtrConst;

  // Constructors & Destructors.
  explicit PgDocCompoundOp(const PgSession::ScopedRefPtr& pg_session);

  Result<RequestSent> Execute(bool force_non_bufferable) override {
    return RequestSent::kTrue;
  }

  Result<string> GetResult() override { return string(); }

 private:
  std::vector<PgDocOp::SharedPtr> ops_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DOC_OP_H_
