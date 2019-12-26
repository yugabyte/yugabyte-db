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

  // Mark this operation as aborted and wait for it to finish.
  void AbortAndWait();

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
  virtual void InitUnlocked(std::unique_lock<std::mutex>* lock);
  virtual CHECKED_STATUS SendRequestUnlocked() = 0;

  // Caching and reading return result.
  void WriteToCacheUnlocked(std::shared_ptr<client::YBPgsqlOp> yb_op);
  void ReadFromCacheUnlocked(string* result);

  // Send another request if no request is pending and we've already consumed
  // all data in the cache.
  CHECKED_STATUS SendRequestIfNeededUnlocked();

  // Sets exec_status_ based on the operation result.
  void HandleResponseStatus(client::YBPgsqlOp* op);

  // Session control.
  PgSession::ScopedRefPtr pg_session_;

  // Operation time. This time is set at the start and must stay the same for the lifetime of the
  // operation to ensure that it is operating on one snapshot.
  uint64_t read_time_ = 0;

  // This mutex protects the fields below.
  mutable std::mutex mtx_;
  std::condition_variable cv_;

  // Result set either from selected or returned targets is cached in a list of strings.
  // Querying state variables.
  Status exec_status_ = Status::OK();

  // Whether or not we are waiting for a response from DocDB after sending a request. Only one
  // request can be sent to DocDB at a time.
  bool waiting_for_response_ = false;

  // Whether all requested data by the statement has been received or there's a run-time error.
  bool end_of_data_ = false;

  // Whether or not result_cache_ is empty().
  bool has_cached_data_ = false;

  // Whether or not the statement has been canceled by application / users.
  bool is_canceled_ = false;

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
  PgDocReadOp(PgSession::ScopedRefPtr pg_session, client::YBPgsqlReadOp *read_op);
  virtual ~PgDocReadOp();

  // Access function.
  std::shared_ptr<client::YBPgsqlReadOp> read_op() {
    return read_op_;
  }

 private:
  // Process response from DocDB.
  void InitUnlocked(std::unique_lock<std::mutex>* lock) override;
  CHECKED_STATUS SendRequestUnlocked() override;
  virtual void ReceiveResponse(Status exec_status);

  // Analyze options and pick the appropriate prefetch limit.
  void SetRequestPrefetchLimit();

  // Set the row_mark_type field of our read request based on our exec control parameter.
  void SetRowMark();

  // Operator.
  std::shared_ptr<client::YBPgsqlReadOp> read_op_;
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
  virtual ~PgDocWriteOp();

  // Access function.
  std::shared_ptr<client::YBPgsqlWriteOp> write_op() {
    return write_op_;
  }

 private:
  // Process response from DocDB.
  CHECKED_STATUS SendRequestUnlocked() override;
  virtual void ReceiveResponse(Status exec_status);

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
  virtual ~PgDocCompoundOp();

  virtual Result<RequestSent> Execute() {
    return RequestSent::kTrue;
  }
  virtual CHECKED_STATUS GetResult(string *result_set) {
    return Status::OK();
  }

 private:
  std::vector<PgDocOp::SharedPtr> ops_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DOC_OP_H_
