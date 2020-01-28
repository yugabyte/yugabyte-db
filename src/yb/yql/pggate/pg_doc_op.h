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

#include <mutex>
#include <condition_variable>

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

  typedef scoped_refptr<PgDocOp> ScopedRefPtr;

  // Constructors & Destructors.
  explicit PgDocOp(PgSession::ScopedRefPtr pg_session);
  virtual ~PgDocOp();

  // Set execution control parameters.
  // When "exec_params" is null, the default setup in PgExecParameters are used.
  void SetExecParams(const PgExecParameters *exec_params);

  // Execute the op. Return true if the request has been sent and is awaiting the result.
  virtual Result<RequestSent> Execute();

  // Get the result of the op.
  virtual CHECKED_STATUS GetResult(string *result_set);

  // Access functions.
  Status exec_status() {
    return exec_status_;
  }
  Result<bool> EndOfResult() const;

  int32_t GetRowsAffectedCount() {
    return rows_affected_count_;
  }

 protected:
  virtual void Init();
  virtual CHECKED_STATUS SendRequest() = 0;
  virtual void ProcessResponse(const Status& exec_status) = 0;

  // Caching and reading return result.
  void WriteToCache(client::YBPgsqlOp* yb_op);
  void ReadFromCache(string* result);

  // Sets exec_status_ based on the operation result.
  void HandleResponseStatus(client::YBPgsqlOp* op);

  // Session control.
  PgSession::ScopedRefPtr pg_session_;

  // Operation time. This time is set at the start and must stay the same for the lifetime of the
  // operation to ensure that it is operating on one snapshot.
  uint64_t read_time_ = 0;

  // Result set either from selected or returned targets is cached in a list of strings.
  // Querying state variables.
  Status exec_status_ = Status::OK();

  // Future object to fetch a response from DocDB after sending a request.
  // Object's valid() method returns false in case no request is sent
  // or sent request was buffered by the session.
  // Only one request can be sent to DocDB at a time.
  PgSessionAsyncRunResult response_;

  // Whether all requested data by the statement has been received or there's a run-time error.
  bool end_of_data_ = false;

  // Caching state variables.
  std::list<string> result_cache_;

  // Exec control parameters.
  PgExecParameters exec_params_;

  // Number of rows affected by the operation.
  int32_t rows_affected_count_ = 0;
};

class PgDocReadOp : public PgDocOp {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocReadOp> SharedPtr;
  typedef std::shared_ptr<const PgDocReadOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocReadOp> UniPtr;
  typedef std::unique_ptr<const PgDocReadOp> UniPtrConst;

  typedef scoped_refptr<PgDocReadOp> ScopedRefPtr;

  // Constructors & Destructors.
  PgDocReadOp(PgSession::ScopedRefPtr pg_session,
              PgTableDesc::ScopedRefPtr table_desc);

  client::YBPgsqlReadOp& GetTemplateOp() {
    return *template_op_;
  }

 private:
  // Process response from DocDB.
  void Init() override;
  CHECKED_STATUS SendRequest() override;
  void ProcessResponse(const Status& exec_status) override;

  // Analyze options and pick the appropriate prefetch limit.
  void SetRequestPrefetchLimit();

  // Set the row_mark_type field of our read request based on our exec control parameter.
  void SetRowMark();

  PgTableDesc::ScopedRefPtr table_desc_;

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

  typedef scoped_refptr<PgDocWriteOp> ScopedRefPtr;

  // Constructors & Destructors.
  PgDocWriteOp(PgSession::ScopedRefPtr pg_session, client::YBPgsqlWriteOp *write_op);

  // Access function.
  std::shared_ptr<client::YBPgsqlWriteOp> write_op() {
    return write_op_;
  }

 private:
  // Process response from DocDB.
  CHECKED_STATUS SendRequest() override;
  virtual void ProcessResponse(const Status& exec_status) override;

  // Operator.
  std::shared_ptr<client::YBPgsqlWriteOp> write_op_;
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

  typedef scoped_refptr<PgDocCompoundOp> ScopedRefPtr;

  // Constructors & Destructors.
  explicit PgDocCompoundOp(PgSession::ScopedRefPtr pg_session);

  Result<RequestSent> Execute() override {
    return RequestSent::kTrue;
  }
  CHECKED_STATUS GetResult(string *result_set) override {
    return Status::OK();
  }

 private:
  std::vector<PgDocOp::SharedPtr> ops_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DOC_OP_H_
