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
//

#pragma once

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.pb.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb_statistics.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/operation_counter.h"
#include "yb/util/result.h"

namespace yb {

class WriteBuffer;

namespace tablet {

class TabletRetentionPolicy;

class AbstractTablet {
 public:
  virtual ~AbstractTablet() {}

  virtual docdb::DocReadContextPtr GetDocReadContext() const = 0;
  virtual Result<docdb::DocReadContextPtr> GetDocReadContext(const std::string& table_id) const = 0;

  virtual TableType table_type() const = 0;

  virtual const std::string& tablet_id() const = 0;

  virtual bool system() const = 0;

  //------------------------------------------------------------------------------------------------
  // Redis support.
  virtual Status HandleRedisReadRequest(
      const docdb::ReadOperationData& read_operation_data,
      const RedisReadRequestPB& redis_read_request,
      RedisResponsePB* response) = 0;

  //------------------------------------------------------------------------------------------------
  // CQL support.
  virtual Status HandleQLReadRequest(
      const docdb::ReadOperationData& read_operation_data,
      const QLReadRequestPB& ql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      QLReadRequestResult* result,
      WriteBuffer* rows_data) = 0;

  Status HandleQLReadRequest(
      const docdb::ReadOperationData& read_operation_data,
      const QLReadRequestPB& ql_read_request,
      const TransactionOperationContext& txn_op_context,
      const docdb::YQLStorageIf& ql_storage,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      QLReadRequestResult* result,
      WriteBuffer* rows_data);

  virtual Status CreatePagingStateForRead(const QLReadRequestPB& ql_read_request,
                                                  const size_t row_count,
                                                  QLResponsePB* response) const = 0;

  virtual TabletRetentionPolicy* RetentionPolicy() = 0;

  // Returns safe timestamp to read.
  // `require_lease` - whether this read requires a hybrid time leader lease. Typically, strongly
  //    consistent reads require a lease, while eventually consistent reads don't.
  // `min_allowed` - result should be greater or equal to `min_allowed`, otherwise
  //    this function tries to wait until the safe time reaches this value or `deadline` happens.
  //
  // Returns invalid hybrid time in case it cannot satisfy provided requirements, e.g. because of
  // a timeout.
  Result<HybridTime> SafeTime(RequireLease require_lease = RequireLease::kTrue,
                              HybridTime min_allowed = HybridTime::kMin,
                              CoarseTimePoint deadline = CoarseTimePoint::max()) const;

  template <class PB>
  Result<IsolationLevel> GetIsolationLevelFromPB(const PB& pb) {
    if (!pb.has_transaction()) {
      return IsolationLevel::NON_TRANSACTIONAL;
    }
    return GetIsolationLevel(pb.transaction());
  }

  virtual Status HandlePgsqlReadRequest(
      const docdb::ReadOperationData& read_operation_data,
      bool is_explicit_request_read_time,
      const PgsqlReadRequestPB& ql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      const SubTransactionMetadataPB& subtransaction_metadata,
      PgsqlReadRequestResult* result) = 0;

  virtual Result<IsolationLevel> GetIsolationLevel(const LWTransactionMetadataPB& transaction) = 0;
  virtual Result<IsolationLevel> GetIsolationLevel(const TransactionMetadataPB& transaction) = 0;

  //-----------------------------------------------------------------------------------------------
  // PGSQL support.
  //-----------------------------------------------------------------------------------------------

  virtual Status CreatePagingStateForRead(const PgsqlReadRequestPB& pgsql_read_request,
                                                  const size_t row_count,
                                                  PgsqlResponsePB* response) const = 0;

  Status ProcessPgsqlReadRequest(const docdb::ReadOperationData& read_operation_data,
                                 bool is_explicit_request_read_time,
                                 const PgsqlReadRequestPB& pgsql_read_request,
                                 const std::shared_ptr<TableInfo>& table_info,
                                 const TransactionOperationContext& txn_op_context,
                                 const docdb::YQLStorageIf& ql_storage,
                                 std::reference_wrapper<const ScopedRWOperation> pending_op,
                                 PgsqlReadRequestResult* result);

  virtual bool IsTransactionalRequest(bool is_ysql_request) const = 0;

 private:
  virtual Result<HybridTime> DoGetSafeTime(
      RequireLease require_lease, HybridTime min_allowed, CoarseTimePoint deadline) const = 0;
};

}  // namespace tablet
}  // namespace yb
