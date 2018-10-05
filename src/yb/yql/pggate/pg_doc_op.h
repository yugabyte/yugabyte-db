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

#include "yb/util/locks.h"
#include "yb/util/semaphore.h"
#include "yb/client/yb_op.h"
#include "yb/yql/pggate/pg_session.h"

namespace yb {
namespace pggate {

class PgDocOp {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocOp> SharedPtr;
  typedef std::shared_ptr<const PgDocOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocOp> UniPtr;
  typedef std::unique_ptr<const PgDocOp> UniPtrConst;

  typedef scoped_refptr<PgDocOp> ScopedRefPtr;

  // Public constants.
  static const int64_t kPrefetchLimit = INT32_MAX;

  // Constructors & Destructors.
  explicit PgDocOp(PgSession::ScopedRefPtr pg_session);
  virtual ~PgDocOp();

  // Postgres Ops.
  virtual CHECKED_STATUS Execute();
  virtual CHECKED_STATUS GetResult(string *result_set);

  // Access functions.
  Status exec_status() {
    return exec_status_;
  }
  bool EndOfResult() const;

 protected:
  // Processing response from DocDB.
  virtual void Init();
  virtual CHECKED_STATUS SendRequest() = 0;
  void WaitForData();

  // Caching and reading return result.
  virtual void WriteToCache(std::shared_ptr<client::YBPgsqlOp> yb_op);
  virtual void ReadFromCache(string *result);

  // Session control.
  PgSession::ScopedRefPtr pg_session_;

  // Result set either from selected or returned targets is cached in a list of strings.
  // Querying state variables.
  Status exec_status_ = Status::OK();

  // Execution semaphore is used to lock a FETCH when waiting for response from DocDB.
  Semaphore exec_semaphore_;

  // Whether or not we are waiting for a response from DocDB after sending a request. Only one
  // request can be sent to DocDB at a time.
  bool waiting_for_response_ = false;

  // Whether all requested data by the statement has been received or there's a run-time error.
  bool end_of_data_ = false;

  // Whether or not result_cache_ is empty(). Instead of access result_cache_, which requires a
  // lock, we use this boolean state variable.
  bool has_cached_data_ = false;

  // Whether or not the statement has been canceled by application / users.
  bool is_canceled_ = false;

  // Caching state variables.
  simple_spinlock cache_lock_;
  std::list<string> result_cache_;
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
  virtual void Init() override;
  virtual CHECKED_STATUS SendRequest() override;
  virtual void ReceiveResponse(Status exec_status);

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
  virtual CHECKED_STATUS SendRequest() override;
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

  virtual CHECKED_STATUS Execute() {
    return Status::OK();
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
