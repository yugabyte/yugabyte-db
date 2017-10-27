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

namespace yb {
namespace tablet {

class AbstractTablet {
 public:
  virtual ~AbstractTablet() {}

  virtual const Schema& SchemaRef() const = 0;

  virtual const common::QLStorageIf& QLStorage() const = 0;

  virtual TableType table_type() const = 0;

  virtual const std::string& tablet_id() const = 0;

  virtual CHECKED_STATUS HandleRedisReadRequest(
      HybridTime timestamp, const RedisReadRequestPB& redis_read_request,
      RedisResponsePB* response) = 0;

  virtual CHECKED_STATUS HandleQLReadRequest(
      HybridTime timestamp, const QLReadRequestPB& ql_read_request,
      const TransactionMetadataPB& transaction_metadata, QLResponsePB* response,
      gscoped_ptr<faststring>* rows_data) = 0;

  virtual CHECKED_STATUS CreatePagingStateForRead(const QLReadRequestPB& ql_read_request,
                                                  const size_t row_count,
                                                  QLResponsePB* response) const = 0;

  virtual void RegisterReaderTimestamp(HybridTime read_point) = 0;
  virtual void UnregisterReader(HybridTime read_point) = 0;
  virtual HybridTime SafeTimestampToRead() const = 0;

 protected:
  CHECKED_STATUS HandleQLReadRequest(
      HybridTime timestamp, const QLReadRequestPB& ql_read_request,
      const TransactionOperationContextOpt& txn_op_context, QLResponsePB* response,
      gscoped_ptr<faststring>* rows_data);
};

}  // namespace tablet
}  // namespace yb

#endif // YB_TABLET_ABSTRACT_TABLET_H
