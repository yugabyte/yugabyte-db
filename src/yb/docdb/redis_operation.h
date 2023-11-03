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

#include <boost/optional/optional.hpp>

#include "yb/common/redis_protocol.pb.h"

#include "yb/docdb/deadline_info.h"
#include "yb/docdb/doc_operation.h"
#include "yb/dockv/expiration.h"
#include "yb/docdb/key_bounds.h"

#include "yb/rocksdb/cache.h"

namespace yb {
namespace docdb {

// Redis value data with attached type of this value.
// Used internally by RedisWriteOperation.
struct RedisValue {
  RedisDataType type = static_cast<RedisDataType>(0);
  std::string value;
  dockv::Expiration exp;
  int64_t internal_index = 0;
};

class RedisWriteOperation :
    public DocOperationBase<DocOperationType::REDIS_WRITE_OPERATION, RedisWriteRequestPB> {
 public:
  // Construct a RedisWriteOperation. Content of request will be swapped out by the constructor.
  explicit RedisWriteOperation(std::reference_wrapper<const RedisWriteRequestPB> request)
      : DocOperationBase(request) {
  }

  bool RequireReadSnapshot() const override { return false; }

  Status Apply(const DocOperationApplyData& data) override;

  Status GetDocPaths(
      GetDocPathsMode mode, DocPathsToLock *paths, IsolationLevel *level) const override;

  RedisResponsePB &response() { return response_; }

 private:
  void ClearResponse() override {
    response_.Clear();
  }

  void InitializeIterator(const DocOperationApplyData& data);
  Result<RedisDataType> GetValueType(const DocOperationApplyData& data,
      int subkey_index = kNilSubkeyIndex);
  Result<RedisValue> GetValue(const DocOperationApplyData& data,
      int subkey_index = kNilSubkeyIndex, dockv::Expiration* exp = nullptr);

  Status ApplySetTtl(const DocOperationApplyData& data);
  Status ApplySet(const DocOperationApplyData& data);
  Status ApplyGetSet(const DocOperationApplyData& data);
  Status ApplyAppend(const DocOperationApplyData& data);
  Status ApplyDel(const DocOperationApplyData& data);
  Status ApplySetRange(const DocOperationApplyData& data);
  Status ApplyIncr(const DocOperationApplyData& data);
  Status ApplyPush(const DocOperationApplyData& data);
  Status ApplyInsert(const DocOperationApplyData& data);
  Status ApplyPop(const DocOperationApplyData& data);
  Status ApplyAdd(const DocOperationApplyData& data);
  Status ApplyRemove(const DocOperationApplyData& data);

  RedisResponsePB response_;
  // TODO: Currently we have a separate iterator per operation, but in future, we leave the option
  // open for operations to share iterators.
  std::unique_ptr<IntentAwareIterator> iterator_;

  rocksdb::QueryId redis_query_id() { return reinterpret_cast<rocksdb::QueryId > (&request_); }
};

class RedisReadOperation {
 public:
  explicit RedisReadOperation(const yb::RedisReadRequestPB& request,
                              const DocDB& doc_db,
                              const ReadOperationData& read_operation_data)
      : request_(request), doc_db_(doc_db), read_operation_data_(read_operation_data) {}

  Status Execute();

  const RedisResponsePB &response();

 private:
  Result<RedisDataType> GetValueType(int subkey_index = kNilSubkeyIndex);

  // GetValue when always_override should be true.
  // This is particularly relevant for the Timeseries datatype, for which
  // child TTLs override parent TTLs.
  // TODO: Once the timeseries bug is fixed, this function as well as the
  // corresponding field can be safely removed.
  Result<RedisValue>GetOverrideValue(int subkey_index = kNilSubkeyIndex);

  Result<RedisValue> GetValue(int subkey_index = kNilSubkeyIndex);

  Status ExecuteGet();
  Status ExecuteGet(const RedisGetRequestPB& get_request);
  Status ExecuteGet(RedisGetRequestPB::GetRequestType type);
  Status ExecuteGetForRename();
  Status ExecuteGetTtl();
  // Used to implement HGETALL, HKEYS, HVALS, SMEMBERS, HLEN, SCARD
  Status ExecuteHGetAllLikeCommands(
      dockv::ValueEntryType value_type, bool add_keys, bool add_values);
  Status ExecuteStrLen();
  Status ExecuteExists();
  Status ExecuteGetRange();
  Status ExecuteCollectionGetRange();
  Status ExecuteCollectionGetRangeByBounds(
      RedisCollectionGetRangeRequestPB::GetRangeRequestType request_type, bool add_keys);
  Status ExecuteCollectionGetRangeByBounds(
      RedisCollectionGetRangeRequestPB::GetRangeRequestType request_type,
      const RedisSubKeyBoundPB& lower_bound, const RedisSubKeyBoundPB& upper_bound, bool add_keys);
  Status ExecuteKeys();

  rocksdb::QueryId redis_query_id() { return reinterpret_cast<rocksdb::QueryId> (&request_); }

  const RedisReadRequestPB& request_;
  RedisResponsePB response_;
  const DocDB doc_db_;
  const ReadOperationData read_operation_data_;
  // TODO: Move iterator_ to a superclass of RedisWriteOperation RedisReadOperation
  // Make these two classes similar in terms of how rocksdb state is passed to them.
  // Currently ReadOperations get the state during construction, but Write operations get them when
  // calling Apply(). Apply() and Execute() should be more similar() in definition.
  std::unique_ptr<IntentAwareIterator> iterator_;

  boost::optional<DeadlineInfo> deadline_info_;
};

}  // namespace docdb
}  // namespace yb
