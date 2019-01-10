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

#ifndef YB_TABLET_ABSTRACT_TABLET_H
#define YB_TABLET_ABSTRACT_TABLET_H

#include "yb/common/redis_protocol.pb.h"
#include "yb/common/schema.h"
#include "yb/common/ql_storage_interface.h"

#include "yb/tablet/tablet_fwd.h"

namespace yb {
namespace tablet {

struct QLReadRequestResult {
  QLResponsePB response;
  faststring rows_data;
  HybridTime restart_read_ht;
};

struct PgsqlReadRequestResult {
  PgsqlResponsePB response;
  faststring rows_data;
  HybridTime restart_read_ht;
};

class AbstractTablet {
 public:
  virtual ~AbstractTablet() {}

  virtual const Schema& SchemaRef(const std::string& table_id = "") const = 0;

  virtual const common::YQLStorageIf& QLStorage() const = 0;

  virtual TableType table_type() const = 0;

  virtual const std::string& tablet_id() const = 0;

  //------------------------------------------------------------------------------------------------
  // Redis support.
  virtual CHECKED_STATUS HandleRedisReadRequest(
      MonoTime deadline,
      const ReadHybridTime& read_time,
      const RedisReadRequestPB& redis_read_request,
      RedisResponsePB* response) = 0;

  //------------------------------------------------------------------------------------------------
  // CQL support.
  virtual CHECKED_STATUS HandleQLReadRequest(
      MonoTime deadline,
      const ReadHybridTime& read_time,
      const QLReadRequestPB& ql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      QLReadRequestResult* result) = 0;

  virtual CHECKED_STATUS CreatePagingStateForRead(const QLReadRequestPB& ql_read_request,
                                                  const size_t row_count,
                                                  QLResponsePB* response) const = 0;

  virtual CHECKED_STATUS RegisterReaderTimestamp(HybridTime read_point) = 0;
  virtual void UnregisterReader(HybridTime read_point) = 0;

  // Returns safe timestamp to read.
  // `require_lease` - whether this read requires a hybrid time leader lease. Typically, strongly
  //    consistent reads require a lease, while eventually consistent reads don't.
  // `min_allowed` - result should be greater or equal to `min_allowed`, otherwise
  //    this function tries to wait until the safe time reaches this value or `deadline` happens.
  //
  // Returns invalid hybrid time in case it cannot satisfy provided requirements, e.g. because of
  // a timeout.
  HybridTime SafeTime(RequireLease require_lease = RequireLease::kTrue,
                      HybridTime min_allowed = HybridTime::kMin,
                      MonoTime deadline = MonoTime::kMax) const {
    return DoGetSafeTime(require_lease, min_allowed, deadline);
  }

 protected:
  CHECKED_STATUS HandleQLReadRequest(
      MonoTime deadline,
      const ReadHybridTime& read_time,
      const QLReadRequestPB& ql_read_request,
      const TransactionOperationContextOpt& txn_op_context,
      QLReadRequestResult* result);


  //------------------------------------------------------------------------------------------------
  // PGSQL support.
 public:
  virtual CHECKED_STATUS HandlePgsqlReadRequest(
      MonoTime deadline,
      const ReadHybridTime& read_time,
      const PgsqlReadRequestPB& ql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      PgsqlReadRequestResult* result) = 0;

  virtual CHECKED_STATUS CreatePagingStateForRead(const PgsqlReadRequestPB& pgsql_read_request,
                                                  const size_t row_count,
                                                  PgsqlResponsePB* response) const = 0;

  CHECKED_STATUS HandlePgsqlReadRequest(MonoTime deadline,
                                        const ReadHybridTime& read_time,
                                        const PgsqlReadRequestPB& pgsql_read_request,
                                        const TransactionOperationContextOpt& txn_op_context,
                                        PgsqlReadRequestResult* result);
 private:
  virtual HybridTime DoGetSafeTime(
      RequireLease require_lease, HybridTime min_allowed, MonoTime deadline) const = 0;
};

}  // namespace tablet
}  // namespace yb

#endif // YB_TABLET_ABSTRACT_TABLET_H
