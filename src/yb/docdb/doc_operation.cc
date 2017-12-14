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

#include "yb/common/partition.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/ql_storage_interface.h"
#include "yb/common/ql_value.h"
#include "yb/common/ql_expr.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/doc_expr.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/subdocument.h"
#include "yb/server/hybrid_clock.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/trace.h"

DECLARE_bool(trace_docdb_calls);

using strings::Substitute;

DEFINE_bool(emulate_redis_responses,
    true,
    "If emulate_redis_responses is false, we hope to get slightly better performance by just "
    "returning OK for commands that might require us to read additional records viz. SADD, HSET, "
    "and HDEL. If emulate_redis_responses is true, we read the required records to compute the "
    "response as specified by the official Redis API documentation. https://redis.io/commands");

namespace yb {
namespace docdb {

using std::set;
using std::list;
using strings::Substitute;

void KuduWriteOperation::GetDocPathsToLock(list<DocPath> *paths, IsolationLevel *level) const {
  paths->push_back(doc_path_);
  *level = IsolationLevel::SNAPSHOT_ISOLATION;
}

Status KuduWriteOperation::Apply(const DocOperationApplyData& data) {
  return data.doc_write_batch->SetPrimitive(doc_path_, value_);
}

void RedisWriteOperation::GetDocPathsToLock(list<DocPath> *paths, IsolationLevel *level) const {
  paths->push_back(DocPath::DocPathFromRedisKey(request_.key_value().hash_code(),
                                                request_.key_value().key()));
  *level = IsolationLevel::SNAPSHOT_ISOLATION;
}

namespace {

bool EmulateRedisResponse(const RedisDataType& data_type) {
  return FLAGS_emulate_redis_responses && data_type != REDIS_TYPE_TIMESERIES;
}

CHECKED_STATUS PrimitiveValueFromSubKey(const RedisKeyValueSubKeyPB &subkey_pb,
                                        PrimitiveValue *primitive_value) {
  switch (subkey_pb.subkey_case()) {
    case RedisKeyValueSubKeyPB::SubkeyCase::kStringSubkey:
      *primitive_value = PrimitiveValue(subkey_pb.string_subkey());
      break;
    case RedisKeyValueSubKeyPB::SubkeyCase::kTimestampSubkey:
      // We use descending order for the timestamp in the timeseries type so that the latest
      // value sorts on top.
      *primitive_value = PrimitiveValue(subkey_pb.timestamp_subkey(), SortOrder::kDescending);
      break;
    default:
      return STATUS_SUBSTITUTE(IllegalState, "Invalid enum value $0", subkey_pb.subkey_case());
  }
  return Status::OK();
}

// Stricter version of the above when we know the exact datatype to expect.
CHECKED_STATUS PrimitiveValueFromSubKeyStrict(const RedisKeyValueSubKeyPB &subkey_pb,
                                              const RedisDataType &data_type,
                                              PrimitiveValue *primitive_value) {
  switch (data_type) {
    case REDIS_TYPE_LIST: FALLTHROUGH_INTENDED;
    case REDIS_TYPE_SET: FALLTHROUGH_INTENDED;
    case REDIS_TYPE_SORTEDSET: FALLTHROUGH_INTENDED;
    case REDIS_TYPE_HASH:
      if (!subkey_pb.has_string_subkey()) {
        return STATUS_SUBSTITUTE(InvalidArgument, "subkey: $0 should be of string type",
                                 subkey_pb.ShortDebugString());
      }
      break;
    case REDIS_TYPE_TIMESERIES:
      if (!subkey_pb.has_timestamp_subkey()) {
        return STATUS_SUBSTITUTE(InvalidArgument, "subkey: $0 should be of int64 type",
                                 subkey_pb.ShortDebugString());
      }
      break;
    default:
      return STATUS_SUBSTITUTE(IllegalState, "Invalid enum value $0", data_type);
  }
  return PrimitiveValueFromSubKey(subkey_pb, primitive_value);
}

Result<RedisDataType> GetRedisValueType(
    rocksdb::DB* rocksdb,
    const ReadHybridTime& read_time,
    const RedisKeyValuePB &key_value_pb,
    rocksdb::QueryId redis_query_id,
    DocWriteBatch* doc_write_batch = nullptr,
    int subkey_index = -1) {
  if (!key_value_pb.has_key()) {
    return STATUS(Corruption, "Expected KeyValuePB");
  }
  SubDocKey subdoc_key;
  if (subkey_index < 0) {
    subdoc_key = SubDocKey(DocKey::FromRedisKey(key_value_pb.hash_code(), key_value_pb.key()));
  } else {
    if (subkey_index >= key_value_pb.subkey_size()) {
      return STATUS_SUBSTITUTE(InvalidArgument,
                               "Size of subkeys ($0) must be larger than subkey_index ($1)",
                               key_value_pb.subkey_size(), subkey_index);
    }

    PrimitiveValue subkey_primitive;
    RETURN_NOT_OK(PrimitiveValueFromSubKey(key_value_pb.subkey(subkey_index), &subkey_primitive));
    subdoc_key = SubDocKey(DocKey::FromRedisKey(key_value_pb.hash_code(), key_value_pb.key()),
                           subkey_primitive);
  }
  SubDocument doc;
  bool doc_found = false;

  // Use the cached entry if possible to determine the value type.
  boost::optional<DocWriteBatchCache::Entry> cached_entry;
  if (doc_write_batch) {
    cached_entry = doc_write_batch->LookupCache(subdoc_key.Encode());
  }
  if (cached_entry) {
    doc_found = true;
    doc = SubDocument(cached_entry->value_type);
  } else {
    // TODO(dtxn) - pass correct transaction context when we implement cross-shard transactions
    // support for Redis.
    GetSubDocumentData data = { &subdoc_key, &doc, &doc_found };
    data.return_type_only = true;
    RETURN_NOT_OK(GetSubDocument(
        rocksdb, data, redis_query_id, boost::none /* txn_op_context */, read_time));
  }

  if (!doc_found) {
    return REDIS_TYPE_NONE;
  }

  switch (doc.value_type()) {
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED;
    case ValueType::kTombstone:
      return REDIS_TYPE_NONE;
    case ValueType::kObject:
      return REDIS_TYPE_HASH;
    case ValueType::kRedisSet:
      return REDIS_TYPE_SET;
    case ValueType::kRedisTS:
      return REDIS_TYPE_TIMESERIES;
    case ValueType::kNull: FALLTHROUGH_INTENDED; // This value is a set member.
    case ValueType::kString:
      return REDIS_TYPE_STRING;
    default:
      return STATUS_FORMAT(Corruption,
                           "Unknown value type for redis record: $0",
                           static_cast<char>(doc.value_type()));
  }
}

Result<RedisValue> GetRedisValue(
    rocksdb::DB *rocksdb,
    const ReadHybridTime& read_time,
    const RedisKeyValuePB &key_value_pb,
    rocksdb::QueryId redis_query_id,
    int subkey_index = -1) {
  if (!key_value_pb.has_key()) {
    return STATUS(Corruption, "Expected KeyValuePB");
  }
  SubDocKey doc_key(DocKey::FromRedisKey(key_value_pb.hash_code(), key_value_pb.key()));

  if (!key_value_pb.subkey().empty()) {
    if (key_value_pb.subkey().size() != 1 && subkey_index == -1) {
      return STATUS_SUBSTITUTE(Corruption,
                               "Expected at most one subkey, got $0", key_value_pb.subkey().size());
    }
    PrimitiveValue subkey_primitive;
    RETURN_NOT_OK(PrimitiveValueFromSubKey(
        key_value_pb.subkey(subkey_index == -1 ? 0 : subkey_index),
        &subkey_primitive));
    doc_key.AppendSubKeysAndMaybeHybridTime(subkey_primitive);
  }

  SubDocument doc;
  bool doc_found = false;

  // TODO(dtxn) - pass correct transaction context when we implement cross-shard transactions
  // support for Redis.
  GetSubDocumentData data = { &doc_key, &doc, &doc_found };
  RETURN_NOT_OK(GetSubDocument(
      rocksdb, data, redis_query_id, boost::none /* txn_op_context */, read_time));

  if (!doc_found) {
    return RedisValue{REDIS_TYPE_NONE};
  }

  if (!doc.IsPrimitive()) {
    switch (doc.value_type()) {
      case ValueType::kObject:
        return RedisValue{REDIS_TYPE_HASH};
      case ValueType::kRedisTS:
        return RedisValue{REDIS_TYPE_TIMESERIES};
      case ValueType::kRedisSet:
        return RedisValue{REDIS_TYPE_SET};
      default:
        return STATUS_SUBSTITUTE(IllegalState, "Invalid value type: $0",
                                 static_cast<int>(doc.value_type()));
    }
  }

  return RedisValue{REDIS_TYPE_STRING, doc.GetString()};
}

YB_STRONGLY_TYPED_BOOL(VerifySuccessIfMissing);

// Set response based on the type match. Return whether the type matches what's expected.
bool VerifyTypeAndSetCode(
    const RedisDataType expected_type,
    const RedisDataType actual_type,
    RedisResponsePB *response,
    VerifySuccessIfMissing verify_success_if_missing = VerifySuccessIfMissing::kFalse) {
  if (actual_type == RedisDataType::REDIS_TYPE_NONE) {
    if (verify_success_if_missing) {
      response->set_code(RedisResponsePB_RedisStatusCode_OK);
    } else {
      response->set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
    }
    return verify_success_if_missing;
  }
  if (actual_type != expected_type) {
    response->set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    return false;
  }
  response->set_code(RedisResponsePB_RedisStatusCode_OK);
  return true;
}

bool VerifyTypeAndSetCode(
    const docdb::ValueType expected_type,
    const docdb::ValueType actual_type,
    RedisResponsePB *response) {
  if (actual_type != expected_type) {
    response->set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    return false;
  }
  response->set_code(RedisResponsePB_RedisStatusCode_OK);
  return true;
}

CHECKED_STATUS AddPrimitiveValueToResponseArray(const PrimitiveValue& value,
                                                RedisArrayPB* redis_array) {
  if (value.IsString()) {
    redis_array->add_elements(value.GetString());
  } else if (value.IsInt64()) {
    redis_array->add_elements(std::to_string(value.GetInt64()));
  } else {
    return STATUS_SUBSTITUTE(InvalidArgument, "Invalid value type: $0",
                             static_cast<int>(value.value_type()));
  }
  return Status::OK();
}

CHECKED_STATUS CheckUserTimestampForCollections(const UserTimeMicros user_timestamp) {
  if (user_timestamp != Value::kInvalidUserTimestamp) {
    return STATUS(InvalidArgument, "User supplied timestamp is only allowed for "
        "replacing the whole collection");
  }
  return Status::OK();
}

template <typename T>
CHECKED_STATUS PopulateRedisResponseFromInternal(T iter,
                                                 const T& iter_end,
                                                 RedisResponsePB *response,
                                                 bool add_keys,
                                                 bool add_values) {
  response->set_allocated_array_response(new RedisArrayPB());
  for (; iter != iter_end; iter++) {
    const PrimitiveValue& first = iter->first;
    const PrimitiveValue& second = iter->second;
    if (add_keys) {
      RETURN_NOT_OK(AddPrimitiveValueToResponseArray(first, response->mutable_array_response()));
    }
    if (add_values) {
      RETURN_NOT_OK(AddPrimitiveValueToResponseArray(second, response->mutable_array_response()));
    }
  }
  return Status::OK();
}

CHECKED_STATUS PopulateResponseFrom(const SubDocument::ObjectContainer &key_values,
                                    RedisResponsePB *response,
                                    bool add_keys,
                                    bool add_values,
                                    bool reverse = false) {
  if (reverse) {
    return PopulateRedisResponseFromInternal(key_values.rbegin(), key_values.rend(), response,
                                             add_keys, add_values);
  } else {
    return PopulateRedisResponseFromInternal(key_values.begin(), key_values.end(), response,
                                             add_keys, add_values);
  }
}

void SetOptionalInt(RedisDataType type, int64_t value, int64_t none_value,
                    RedisResponsePB* response) {
  response->set_int_response(type == RedisDataType::REDIS_TYPE_NONE ? none_value : value);
}

void SetOptionalInt(RedisDataType type, int64_t value, RedisResponsePB* response) {
  SetOptionalInt(type, value, 0, response);
}

} // anonymous namespace

Status RedisWriteOperation::Apply(const DocOperationApplyData& data) {
  switch (request_.request_case()) {
    case RedisWriteRequestPB::RequestCase::kSetRequest:
      return ApplySet(data);
    case RedisWriteRequestPB::RequestCase::kGetsetRequest:
      return ApplyGetSet(data);
    case RedisWriteRequestPB::RequestCase::kAppendRequest:
      return ApplyAppend(data);
    case RedisWriteRequestPB::RequestCase::kDelRequest:
      return ApplyDel(data);
    case RedisWriteRequestPB::RequestCase::kSetRangeRequest:
      return ApplySetRange(data);
    case RedisWriteRequestPB::RequestCase::kIncrRequest:
      return ApplyIncr(data);
    case RedisWriteRequestPB::RequestCase::kPushRequest:
      return ApplyPush(data);
    case RedisWriteRequestPB::RequestCase::kInsertRequest:
      return ApplyInsert(data);
    case RedisWriteRequestPB::RequestCase::kPopRequest:
      return ApplyPop(data);
    case RedisWriteRequestPB::RequestCase::kAddRequest:
      return ApplyAdd(data);
    case RedisWriteRequestPB::RequestCase::REQUEST_NOT_SET: break;
  }
  return STATUS(Corruption,
      Substitute("Unsupported redis read operation: $0", request_.request_case()));
}

Result<RedisDataType> RedisWriteOperation::GetValueType(
    const DocOperationApplyData& data, int subkey_index) {
  return GetRedisValueType(
      data.doc_write_batch->rocksdb(), data.read_time, request_.key_value(),
      redis_query_id(), data.doc_write_batch, subkey_index);
}

Result<RedisValue> RedisWriteOperation::GetValue(
    const DocOperationApplyData& data, int subkey_index) {
  return GetRedisValue(data.doc_write_batch->rocksdb(), data.read_time,
                       request_.key_value(), redis_query_id(), subkey_index);
}

Status RedisWriteOperation::ApplySet(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();
  const MonoDelta ttl = request_.set_request().has_ttl() ?
      MonoDelta::FromMilliseconds(request_.set_request().ttl()) : Value::kMaxTtl;
  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
  if (kv.subkey_size() > 0) {
    auto data_type = GetValueType(data);
    RETURN_NOT_OK(data_type);
    switch (kv.type()) {
      case REDIS_TYPE_TIMESERIES: FALLTHROUGH_INTENDED;
      case REDIS_TYPE_HASH: {
        if (*data_type != kv.type() && *data_type != REDIS_TYPE_NONE) {
          response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
          return Status::OK();
        }
        SubDocument kv_entries = SubDocument();
        for (int i = 0; i < kv.subkey_size(); i++) {
          PrimitiveValue subkey_value;
          RETURN_NOT_OK(PrimitiveValueFromSubKeyStrict(kv.subkey(i), kv.type(), &subkey_value));
          kv_entries.SetChild(subkey_value,
                              SubDocument(PrimitiveValue(kv.value(i))));
        }

        if (kv.type() == REDIS_TYPE_TIMESERIES) {
          RETURN_NOT_OK(kv_entries.ConvertToRedisTS());
        }

        // For an HSET command (which has only one subkey), we need to read the subkey to find out
        // if the key already existed, and return 0 or 1 accordingly. This read is unnecessary for
        // HMSET and TSADD.
        if (kv.subkey_size() == 1 && EmulateRedisResponse(kv.type())) {
          auto type = GetValueType(data, 0);
          RETURN_NOT_OK(type);
          // For HSET/TSADD, we return 0 or 1 depending on if the key already existed.
          // If flag is false, no int response is returned.
          SetOptionalInt(*type, 0, 1, &response_);
        }
        if (*data_type == REDIS_TYPE_NONE && kv.type() == REDIS_TYPE_TIMESERIES) {
          // Need to insert the document instead of extending it.
          RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
              doc_path, kv_entries, redis_query_id(), ttl));
        } else {
          RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
              doc_path, kv_entries, redis_query_id(), ttl));
        }
        break;
      }
      case REDIS_TYPE_STRING: {
        return STATUS_SUBSTITUTE(InvalidCommand,
            "Redis data type $0 in SET command should not have subkeys", kv.type());
      }
      default:
        return STATUS_SUBSTITUTE(InvalidCommand,
            "Redis data type $0 not supported in SET command", kv.type());
    }
  } else {
    if (kv.type() != REDIS_TYPE_STRING) {
      return STATUS_SUBSTITUTE(InvalidCommand,
          "Redis data type for SET must be string if subkey not present, found $0", kv.type());
    }
    if (kv.value_size() != 1) {
      return STATUS_SUBSTITUTE(InvalidCommand,
          "There must be only one value in SET if there is only one key, found $0",
          kv.value_size());
    }
    const RedisWriteMode mode = request_.set_request().mode();
    if (mode != RedisWriteMode::REDIS_WRITEMODE_UPSERT) {
      auto data_type = GetValueType(data);
      RETURN_NOT_OK(data_type);
      if ((mode == RedisWriteMode::REDIS_WRITEMODE_INSERT && *data_type != REDIS_TYPE_NONE)
          || (mode == RedisWriteMode::REDIS_WRITEMODE_UPDATE && *data_type == REDIS_TYPE_NONE)) {
        response_.set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
        return Status::OK();
      }
    }
    RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
        doc_path, Value(PrimitiveValue(kv.value(0)), ttl),
        redis_query_id()));
  }
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

Status RedisWriteOperation::ApplyGetSet(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();

  auto value = GetValue(data);
  RETURN_NOT_OK(value);

  if (kv.value_size() != 1) {
    return STATUS_SUBSTITUTE(Corruption,
        "Getset kv should have 1 value, found $0", kv.value_size());
  }

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }
  response_.set_string_response(value->value);

  return data.doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(kv.value(0))), redis_query_id());
}

Status RedisWriteOperation::ApplyAppend(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();

  if (kv.value_size() != 1) {
    return STATUS_SUBSTITUTE(Corruption,
        "Append kv should have 1 value, found $0", kv.value_size());
  }

  auto value = GetValue(data);
  RETURN_NOT_OK(value);

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_,
                            VerifySuccessIfMissing::kTrue)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  value->value += kv.value(0);

  response_.set_int_response(value->value.length());

  return data.doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()), Value(PrimitiveValue(value->value)),
      redis_query_id());
}

// TODO (akashnil): Actually check if the value existed, return 0 if not. handle multidel in future.
//                  See ENG-807
Status RedisWriteOperation::ApplyDel(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();
  auto data_type = GetValueType(data);
  RETURN_NOT_OK(data_type);
  if (*data_type != REDIS_TYPE_NONE && *data_type != kv.type() && kv.type() != REDIS_TYPE_NONE) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    return Status::OK();
  }
  SubDocument values =  SubDocument();
  int num_keys;
  if (kv.type() == REDIS_TYPE_NONE) { // Delete any string, or container.
    values = SubDocument(ValueType::kTombstone);
    // Currently we only support deleting one key with DEL command.
    num_keys = *data_type == REDIS_TYPE_NONE ? 0 : 1;
  } else {
    num_keys = kv.subkey_size(); // We know the subkeys are distinct.
    // Avoid reads for redis timeseries type.
    if (EmulateRedisResponse(kv.type())) {
      for (int i = 0; i < kv.subkey_size(); i++) {
        auto type = GetValueType(data, i);
        RETURN_NOT_OK(type);
        if (*type == REDIS_TYPE_STRING) {
          values.SetChild(PrimitiveValue(kv.subkey(i).string_subkey()),
                          SubDocument(ValueType::kTombstone));
        } else {
          // If the key is absent, it doesn't contribute to the count of keys being deleted.
          num_keys--;
        }
      }
    } else {
      // If the key is invalid, GetRedisValueType will set data_type to REDIS_TYPE_NONE because it
      // can't find the key. If the op is TSREM, then the kv.type() should be REDIS_TYPE_TIMESERIES,
      // so we can safely return OK since there is nothing to remove.
      if (kv.type() == REDIS_TYPE_TIMESERIES && *data_type == REDIS_TYPE_NONE) {
        return Status::OK();
      }
      for (int i = 0; i < kv.subkey_size(); i++) {
        PrimitiveValue primitive_value;
        RETURN_NOT_OK(PrimitiveValueFromSubKeyStrict(kv.subkey(i), *data_type, &primitive_value));
        values.SetChild(primitive_value, SubDocument(ValueType::kTombstone));
      }
    }
  }
  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
  RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
      doc_path, values, redis_query_id()));
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  if (EmulateRedisResponse(kv.type())) {
    // If the flag is true, we respond with the number of keys actually being deleted. We don't
    // report this number for the redis timeseries type to avoid reads.
    response_.set_int_response(num_keys);
  }
  return Status::OK();
}

Status RedisWriteOperation::ApplySetRange(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();
  if (kv.value_size() != 1) {
    return STATUS_SUBSTITUTE(Corruption,
        "SetRange kv should have 1 value, found $0", kv.value_size());
  }

  auto value = GetValue(data);
  RETURN_NOT_OK(value);

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_,
                            VerifySuccessIfMissing::kTrue)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  // TODO (akashnil): Handle overflows.
  if (request_.set_range_request().offset() > value->value.length()) {
    value->value.resize(request_.set_range_request().offset(), 0);
  }
  value->value.replace(request_.set_range_request().offset(), kv.value(0).length(), kv.value(0));
  response_.set_int_response(value->value.length());

  return data.doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(value->value)), redis_query_id());
}

Status RedisWriteOperation::ApplyIncr(const DocOperationApplyData& data, int64_t incr) {
  const RedisKeyValuePB& kv = request_.key_value();

  auto value = GetValue(data);
  RETURN_NOT_OK(value);

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  int64_t old_value, new_value;

  try {
    old_value = std::stoll(value->value);
    new_value = old_value + incr;
  } catch (std::invalid_argument e) {
    response_.set_error_message("Can not parse incr argument as a number");
    return Status::OK();
  } catch (std::out_of_range e) {
    response_.set_error_message("Can not parse incr argument as a number");
    return Status::OK();
  }

  if ((incr < 0 && old_value < 0 && incr < numeric_limits<int64_t>::min() - old_value) ||
      (incr > 0 && old_value > 0 && incr > numeric_limits<int64_t>::max() - old_value)) {
    response_.set_error_message("Increment would overflow");
    return Status::OK();
  }

  response_.set_int_response(new_value);

  return data.doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(std::to_string(new_value))),
      redis_query_id());
}

Status RedisWriteOperation::ApplyPush(const DocOperationApplyData& data) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

Status RedisWriteOperation::ApplyInsert(const DocOperationApplyData& data) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

Status RedisWriteOperation::ApplyPop(const DocOperationApplyData& data) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

Status RedisWriteOperation::ApplyAdd(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();
  auto data_type = GetValueType(data);
  RETURN_NOT_OK(data_type);

  if (*data_type != REDIS_TYPE_SET && *data_type != REDIS_TYPE_NONE) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    return Status::OK();
  }

  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());

  if (kv.subkey_size() == 0) {
    return STATUS(InvalidCommand, "SADD request has no subkeys set");
  }

  int num_keys_found = 0;

  SubDocument set_entries = SubDocument();

  for (int i = 0 ; i < kv.subkey_size(); i++) { // We know that each subkey is distinct.
    if (FLAGS_emulate_redis_responses) {
      auto type = GetValueType(data, i);
      RETURN_NOT_OK(type);
      if (*type != REDIS_TYPE_NONE) {
        num_keys_found++;
      }
    }

    set_entries.SetChild(
        PrimitiveValue(kv.subkey(i).string_subkey()),
        SubDocument(PrimitiveValue(ValueType::kNull)));
  }

  RETURN_NOT_OK(set_entries.ConvertToRedisSet());

  Status s;

  if (*data_type == REDIS_TYPE_NONE) {
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(doc_path, set_entries, redis_query_id()));
  } else {
    RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(doc_path, set_entries, redis_query_id()));
  }

  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  if (FLAGS_emulate_redis_responses) {
    // If flag is set, the actual number of new keys added is sent as response.
    response_.set_int_response(kv.subkey_size() - num_keys_found);
  }
  return Status::OK();
}

Status RedisWriteOperation::ApplyRemove(const DocOperationApplyData& data) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

Status RedisReadOperation::Execute() {
  switch (request_.request_case()) {
    case RedisReadRequestPB::RequestCase::kGetRequest:
      return ExecuteGet();
    case RedisReadRequestPB::RequestCase::kStrlenRequest:
      return ExecuteStrLen();
    case RedisReadRequestPB::RequestCase::kExistsRequest:
      return ExecuteExists();
    case RedisReadRequestPB::RequestCase::kGetRangeRequest:
      return ExecuteGetRange();
    case RedisReadRequestPB::RequestCase::kGetCollectionRangeRequest:
      return ExecuteCollectionGetRange();
    default:
      return STATUS(Corruption,
          Substitute("Unsupported redis write operation: $0", request_.request_case()));
  }
}

int RedisReadOperation::ApplyIndex(int32_t index, const int32_t len) {
  if (index < 0) index += len;
  if (index < 0 || index >= len)
    return -1;
  return index;
}

Status RedisReadOperation::ExecuteHGetAllLikeCommands(ValueType value_type,
                                                      bool add_keys,
                                                      bool add_values) {
  SubDocKey doc_key(
      DocKey::FromRedisKey(request_.key_value().hash_code(), request_.key_value().key()));
  SubDocument doc;
  bool doc_found = false;
  // TODO(dtxn) - pass correct transaction context when we implement cross-shard transactions
  // support for Redis.
  GetSubDocumentData data = { &doc_key, &doc, &doc_found };
  RETURN_NOT_OK(GetSubDocument(
      db_, data, redis_query_id(), boost::none /* txn_op_context */, read_time_));
  if (add_keys || add_values) {
    response_.set_allocated_array_response(new RedisArrayPB());
  }
  if (!doc_found) {
    response_.set_code(RedisResponsePB_RedisStatusCode_OK);
    return Status::OK();
  }
  if (VerifyTypeAndSetCode(value_type, doc.value_type(), &response_)) {
    if (add_keys || add_values) {
      RETURN_NOT_OK(PopulateResponseFrom(doc.object_container(), &response_, add_keys, add_values));
    } else {
      response_.set_int_response(doc.object_container().size());
    }
  }
  return Status::OK();
}

Status RedisReadOperation::ExecuteCollectionGetRange() {
  const RedisKeyValuePB& key_value = request_.key_value();
  if (!request_.has_key_value() || !key_value.has_key() || !request_.has_subkey_range() ||
      !request_.subkey_range().has_lower_bound() || !request_.subkey_range().has_upper_bound()) {
    return STATUS(InvalidArgument, "Need to specify the key and the subkey range");
  }

  const auto request_type = request_.get_collection_range_request().request_type();
  switch (request_type) {
    case RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSRANGEBYTIME: {
      const RedisSubKeyBoundPB& lower_bound = request_.subkey_range().lower_bound();
      const RedisSubKeyBoundPB& upper_bound = request_.subkey_range().upper_bound();

      if ((lower_bound.has_infinity_type() &&
          lower_bound.infinity_type() == RedisSubKeyBoundPB_InfinityType_POSITIVE) ||
          (upper_bound.has_infinity_type() &&
              upper_bound.infinity_type() == RedisSubKeyBoundPB_InfinityType_NEGATIVE)) {
        // Return empty response.
        response_.set_code(RedisResponsePB_RedisStatusCode_OK);
        RETURN_NOT_OK(PopulateResponseFrom(SubDocument::ObjectContainer(), &response_,
            /* add_keys */ true, /* add_values */ true));
        return Status::OK();
      }

      int64_t low_timestamp = lower_bound.subkey_bound().timestamp_subkey();
      int64_t high_timestamp = upper_bound.subkey_bound().timestamp_subkey();

      // Look for the appropriate subdoc.
      SubDocKey doc_key(
          DocKey::FromRedisKey(request_.key_value().hash_code(), request_.key_value().key()));

      // Need to switch the order since we store the timestamps in descending order.
      SubDocKeyBound low_subkey = (upper_bound.has_infinity_type()) ?
          SubDocKeyBound(): SubDocKeyBound(doc_key.doc_key(),
                                           PrimitiveValue(high_timestamp, SortOrder::kDescending),
                                           upper_bound.is_exclusive(), /* is_lower_bound */ true);
      SubDocKeyBound high_subkey = (lower_bound.has_infinity_type()) ?
          SubDocKeyBound(): SubDocKeyBound(doc_key.doc_key(),
                                           PrimitiveValue(low_timestamp, SortOrder::kDescending),
                                           lower_bound.is_exclusive(), /* is_lower_bound */ false);

      SubDocument doc;
      bool doc_found = false;
      GetSubDocumentData data = { &doc_key, &doc, &doc_found };
      data.low_subkey = &low_subkey;
      data.high_subkey = &high_subkey;
      RETURN_NOT_OK(GetSubDocument(
          db_, data, redis_query_id(), boost::none /* txn_op_context */, read_time_));

      // Validate and populate response.
      response_.set_allocated_array_response(new RedisArrayPB());
      if (!doc_found) {
        response_.set_code(RedisResponsePB_RedisStatusCode_OK);
        return Status::OK();
      }
      if (VerifyTypeAndSetCode(ValueType::kRedisTS, doc.value_type(), &response_)) {
        // Need to reverse the order here since we store the timestamps in descending order.
        RETURN_NOT_OK(PopulateResponseFrom(doc.object_container(), &response_, /* add_keys */ true,
            /* add_values */ true, /* reverse */ true));
      }
      break;
    }
    case RedisCollectionGetRangeRequestPB_GetRangeRequestType_UNKNOWN:
      return STATUS(InvalidCommand, "Unknown Collection Get Range Request not supported");
  }
  return Status::OK();
}

Result<RedisDataType> RedisReadOperation::GetValueType(int subkey_index) {
  return GetRedisValueType(db_, read_time_, request_.key_value(), redis_query_id(),
                           nullptr /* doc_write_batch */, subkey_index);

}

Result<RedisValue> RedisReadOperation::GetValue(int subkey_index) {
  return GetRedisValue(db_, read_time_, request_.key_value(), redis_query_id(), subkey_index);
}

Status RedisReadOperation::ExecuteGet() {
  const auto request_type = request_.get_request().request_type();
  switch (request_type) {
    case RedisGetRequestPB_GetRequestType_GET: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_TSGET: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_HGET: {
      auto value = GetValue();
      RETURN_NOT_OK(value);

      // If wrong type, we set the error code in the response.
      if (VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_)) {
        response_.set_string_response(value->value);
      }
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_HEXISTS: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_SISMEMBER: {
      auto type = GetValueType();
      RETURN_NOT_OK(type);
      auto expected_type = request_type == RedisGetRequestPB_GetRequestType_HEXISTS
              ? RedisDataType::REDIS_TYPE_HASH
              : RedisDataType::REDIS_TYPE_SET;
      if (VerifyTypeAndSetCode(expected_type, *type, &response_, VerifySuccessIfMissing::kTrue)) {
        auto subtype = GetValueType(0);
        RETURN_NOT_OK(subtype);
        SetOptionalInt(*subtype, 1, &response_);
      }
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_HSTRLEN: {
      auto type = GetValueType();
      RETURN_NOT_OK(type);
      if (VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_HASH, *type, &response_,
                               VerifySuccessIfMissing::kTrue)) {
        auto value = GetValue();
        RETURN_NOT_OK(value);
        SetOptionalInt(value->type, value->value.length(), &response_);
      }
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_MGET: {
      return STATUS(NotSupported, "MGET not yet supported");
    }
    case RedisGetRequestPB_GetRequestType_HMGET: {
      auto type = GetValueType();
      RETURN_NOT_OK(type);
      if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_HASH, *type, &response_,
                                VerifySuccessIfMissing::kTrue)) {
        return Status::OK();
      }

      response_.set_allocated_array_response(new RedisArrayPB());
      for (int i = 0; i < request_.key_value().subkey_size(); i++) {
        // TODO: ENG-1803: It is inefficient to create a new iterator for each subkey causing a
        // new seek. Consider reusing the same iterator.
        auto value = GetValue(i);
        RETURN_NOT_OK(value);
        if (value->type == REDIS_TYPE_STRING) {
          response_.mutable_array_response()->add_elements(value->value);
        } else {
          response_.mutable_array_response()->add_elements(""); // Empty is nil response.
        }
      }
      response_.set_code(RedisResponsePB_RedisStatusCode_OK);
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_HGETALL:
      return ExecuteHGetAllLikeCommands(ValueType::kObject, true, true);
    case RedisGetRequestPB_GetRequestType_HKEYS:
      return ExecuteHGetAllLikeCommands(ValueType::kObject, true, false);
    case RedisGetRequestPB_GetRequestType_HVALS:
      return ExecuteHGetAllLikeCommands(ValueType::kObject, false, true);
    case RedisGetRequestPB_GetRequestType_HLEN:
      return ExecuteHGetAllLikeCommands(ValueType::kObject, false, false);
    case RedisGetRequestPB_GetRequestType_SMEMBERS:
      return ExecuteHGetAllLikeCommands(ValueType::kRedisSet, true, false);
    case RedisGetRequestPB_GetRequestType_SCARD:
      return ExecuteHGetAllLikeCommands(ValueType::kRedisSet, false, false);
    case RedisGetRequestPB_GetRequestType_UNKNOWN: {
      return STATUS(InvalidCommand, "Unknown Get Request not supported");
    }
  }
  return Status::OK();
}

Status RedisReadOperation::ExecuteStrLen() {
  auto value = GetValue();
  RETURN_NOT_OK(value);

  if (VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_,
                           VerifySuccessIfMissing::kTrue)) {
    SetOptionalInt(value->type, value->value.length(), &response_);
  }

  return Status::OK();
}

Status RedisReadOperation::ExecuteExists() {
  auto value = GetValue();
  RETURN_NOT_OK(value);

  // We only support exist command with one argument currently.
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  SetOptionalInt(value->type, 1, &response_);

  return Status::OK();
}

Status RedisReadOperation::ExecuteGetRange() {
  auto value = GetValue();
  RETURN_NOT_OK(value);

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  const int32_t len = value->value.length();

  // We treat negative indices to refer backwards from the end of the string.
  const int32_t start = ApplyIndex(request_.get_range_request().start(), len);
  if (start == -1) {
    response_.set_code(RedisResponsePB_RedisStatusCode_INDEX_OUT_OF_BOUNDS);
    return Status::OK();
  }
  const int32_t end = ApplyIndex(request_.get_range_request().end(), len);
  if (end == -1 || end < start) {
    response_.set_code(RedisResponsePB_RedisStatusCode_INDEX_OUT_OF_BOUNDS);
    return Status::OK();
  }

  response_.set_string_response(value->value.c_str() + start, end - start + 1);
  return Status::OK();
}

const RedisResponsePB& RedisReadOperation::response() {
  return response_;
}

namespace {

bool RequireReadForExpressions(const QLWriteRequestPB& request) {
  // A QLWriteOperation requires a read if it contains an IF clause or an UPDATE assignment that
  // involves an expresion with a column reference. If the IF clause contains a condition that
  // involves a column reference, the column will be included in "column_refs". However, we cannot
  // rely on non-empty "column_ref" alone to decide if a read is required because "IF EXISTS" and
  // "IF NOT EXISTS" do not involve a column reference explicitly.
  return request.has_if_expr()
      || request.has_column_refs() && (!request.column_refs().ids().empty() ||
          !request.column_refs().static_ids().empty());
}

// If range key portion is missing and there are no targeted columns this is a range operation
// (e.g. range delete) -- it affects all rows within a hash key that match the where clause.
// Note: If target columns are given this could just be e.g. a delete targeting a static column
// which can also omit the range portion -- Analyzer will check these restrictions.
bool IsRangeOperation(const QLWriteRequestPB& request, const Schema& schema) {
  return schema.num_range_key_columns() > 0 &&
         request.range_column_values().empty() &&
         request.column_values().empty();
}

bool RequireRead(const QLWriteRequestPB& request, const Schema& schema) {
  // In case of a user supplied timestamp, we need a read (and hence appropriate locks for read
  // modify write) but it is at the docdb level on a per key basis instead of a QL read of the
  // latest row.
  bool has_user_timestamp = request.has_user_timestamp_usec();

  // We need to read the rows in the given range to find out which rows to write to.
  bool is_range_operation = IsRangeOperation(request, schema);

  return RequireReadForExpressions(request) || has_user_timestamp || is_range_operation;
}

// Create projection schemas of static and non-static columns from a rowblock projection schema
// (for read) and a WHERE / IF condition (for read / write). "schema" is the full table schema
// and "rowblock_schema" is the selected columns from which we are splitting into static and
// non-static column portions.
CHECKED_STATUS CreateProjections(const Schema& schema, const QLReferencedColumnsPB& column_refs,
                                 Schema* static_projection, Schema* non_static_projection) {
  // The projection schemas are used to scan docdb. Keep the columns to fetch in sorted order for
  // more efficient scan in the iterator.
  set<ColumnId> static_columns, non_static_columns;

  // Add regular columns.
  for (int32_t id : column_refs.ids()) {
    const ColumnId column_id(id);
    if (!schema.is_key_column(column_id)) {
      non_static_columns.insert(column_id);
    }
  }

  // Add static columns.
  for (int32_t id : column_refs.static_ids()) {
    const ColumnId column_id(id);
    static_columns.insert(column_id);
  }

  RETURN_NOT_OK(
      schema.CreateProjectionByIdsIgnoreMissing(
          vector<ColumnId>(static_columns.begin(), static_columns.end()),
          static_projection));
  RETURN_NOT_OK(
      schema.CreateProjectionByIdsIgnoreMissing(
          vector<ColumnId>(non_static_columns.begin(), non_static_columns.end()),
          non_static_projection));

  return Status::OK();
}

void PopulateRow(
    const QLTableRow& table_row, const Schema& projection, size_t col_idx, QLRow* row) {
  for (size_t i = 0; i < projection.num_columns(); i++) {
    const auto column_id = projection.column_id(i);
    const auto it = table_row.find(column_id);
    if (it != table_row.end()) {
      *row->mutable_column(col_idx) = std::move(it->second.value);
    }
    col_idx++;
  }
}

// Join a static row with a non-static row.
void JoinStaticRow(
    const Schema& schema, const Schema& static_projection, const QLTableRow& static_row,
    QLTableRow* non_static_row) {
  // No need to join if static row is empty or the hash key is different.
  if (static_row.empty()) {
    return;
  }
  for (size_t i = 0; i < schema.num_hash_key_columns(); i++) {
    const ColumnId column_id = schema.column_id(i);
    if (static_row.at(column_id).value != non_static_row->at(column_id).value) {
      return;
    }
  }

  // Join the static columns in the static row into the non-static row.
  for (size_t i = 0; i < static_projection.num_columns(); i++) {
    const ColumnId column_id = static_projection.column_id(i);
    const auto itr = static_row.find(column_id);
    if (itr != static_row.end()) {
      non_static_row->emplace(column_id, itr->second);
    }
  }
}

} // namespace

QLWriteOperation::QLWriteOperation(
    QLWriteRequestPB* request, const Schema& schema, QLResponsePB* response,
    const TransactionOperationContextOpt& txn_op_context)
    : schema_(schema), response_(response), txn_op_context_(txn_op_context),
      require_read_(RequireRead(*request, schema)) {
  request_.Swap(request);
  // Determine if static / non-static columns are being written.
  bool write_static_columns = false;
  bool write_non_static_columns = false;
  for (const auto& column : request_.column_values()) {
    if (schema.column_by_id(ColumnId(column.column_id())).is_static()) {
      write_static_columns = true;
    } else {
      write_non_static_columns = true;
    }
    if (write_static_columns && write_non_static_columns) {
      break;
    }
  }

  bool is_range_operation = IsRangeOperation(request_, schema);

  // We need the hashed key if writing to the static columns, and need primary key if writing to
  // non-static columns or writing the full primary key (i.e. range columns are present or table
  // does not have range columns).
  CHECK_OK(InitializeKeys(
      write_static_columns || is_range_operation,
      write_non_static_columns ||
      !request_.range_column_values().empty() ||
      schema.num_range_key_columns() == 0));
}

Status QLWriteOperation::InitializeKeys(const bool hashed_key, const bool primary_key) {
  // Populate the hashed and range components in the same order as they are in the table schema.
  const auto& hashed_column_values = request_.hashed_column_values();
  const auto& range_column_values = request_.range_column_values();
  vector<PrimitiveValue> hashed_components;
  vector<PrimitiveValue> range_components;
  RETURN_NOT_OK(QLKeyColumnValuesToPrimitiveValues(
      hashed_column_values, schema_, 0,
      schema_.num_hash_key_columns(), &hashed_components));
  RETURN_NOT_OK(QLKeyColumnValuesToPrimitiveValues(
      range_column_values, schema_, schema_.num_hash_key_columns(),
      schema_.num_range_key_columns(), &range_components));

  // We need the hash key if writing to the static columns.
  if (hashed_key && hashed_doc_key_ == nullptr) {
    hashed_doc_key_.reset(new DocKey(request_.hash_code(), hashed_components));
    hashed_doc_path_.reset(new DocPath(hashed_doc_key_->Encode()));
  }
  // We need the primary key if writing to non-static columns or writing the full primary key
  // (i.e. range columns are present).
  if (primary_key && pk_doc_key_ == nullptr) {
    if (request_.has_hash_code() && !hashed_column_values.empty()) {
      pk_doc_key_.reset(new DocKey(request_.hash_code(), hashed_components, range_components));
    } else {
      // In case of syscatalog tables, we don't have any hash components.
      pk_doc_key_.reset(new DocKey(range_components));
    }
    pk_doc_path_.reset(new DocPath(pk_doc_key_->Encode()));
  }

  return Status::OK();
}

void QLWriteOperation::GetDocPathsToLock(list<DocPath> *paths, IsolationLevel *level) const {
  if (hashed_doc_path_ != nullptr)
    paths->push_back(*hashed_doc_path_);
  if (pk_doc_path_ != nullptr)
    paths->push_back(*pk_doc_path_);
  // When this write operation requires a read, it requires a read snapshot so paths will be locked
  // in snapshot isolation for consistency. Otherwise, pure writes will happen in serializable
  // isolation so that they will serialize but do not conflict with one another.
  //
  // Currently, only keys that are being written are locked, no lock is taken on read at the
  // snapshot isolation level.
  *level = require_read_ ? IsolationLevel::SNAPSHOT_ISOLATION
                         : IsolationLevel::SERIALIZABLE_ISOLATION;
}

Status QLWriteOperation::ReadColumns(const DocOperationApplyData& data,
                                     Schema *param_static_projection,
                                     Schema *param_non_static_projection,
                                     QLTableRow *table_row) {

  Schema *static_projection = param_static_projection;
  Schema *non_static_projection = param_non_static_projection;

  Schema local_static_projection;
  Schema local_non_static_projection;
  if (static_projection == nullptr) {
    static_projection = &local_static_projection;
  }
  if (non_static_projection == nullptr) {
    non_static_projection = &local_non_static_projection;
  }

  // Create projections to scan docdb.
  RETURN_NOT_OK(CreateProjections(schema_, request_.column_refs(),
                                  static_projection, non_static_projection));

  // Generate hashed / primary key depending on if static / non-static columns are referenced in
  // the if-condition.
  RETURN_NOT_OK(InitializeKeys(
      !static_projection->columns().empty(), !non_static_projection->columns().empty()));

  // Scan docdb for the static and non-static columns of the row using the hashed / primary key.
  if (hashed_doc_key_ != nullptr) {
    DocQLScanSpec spec(*static_projection, *hashed_doc_key_, request_.query_id());
    DocRowwiseIterator iterator(*static_projection, schema_, txn_op_context_,
                                data.doc_write_batch->rocksdb(), data.read_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(*static_projection, table_row));
    }
    data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
  }
  if (pk_doc_key_ != nullptr) {
    DocQLScanSpec spec(*non_static_projection, *pk_doc_key_, request_.query_id());
    DocRowwiseIterator iterator(*non_static_projection, schema_, txn_op_context_,
                                data.doc_write_batch->rocksdb(), data.read_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(*non_static_projection, table_row));
    } else {
      // If no non-static column is found, the row does not exist and we should clear the static
      // columns in the map to indicate the row does not exist.
      table_row->clear();
    }
    data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
  }

  return Status::OK();
}

Status QLWriteOperation::IsConditionSatisfied(const QLConditionPB& condition,
                                              const DocOperationApplyData& data,
                                              bool* should_apply,
                                              std::unique_ptr<QLRowBlock>* rowblock,
                                              QLTableRow* table_row) {

  // Read column values.
  Schema static_projection, non_static_projection;
  RETURN_NOT_OK(ReadColumns(data, &static_projection, &non_static_projection, table_row));

  // See if the if-condition is satisfied.
  RETURN_NOT_OK(EvaluateCondition(condition, *table_row, should_apply));

  // Populate the result set to return the "applied" status, and optionally the present column
  // values if the condition is not satisfied and the row does exist (value_map is not empty).
  std::vector<ColumnSchema> columns;
  columns.emplace_back(ColumnSchema("[applied]", BOOL));
  if (!*should_apply && !table_row->empty()) {
    columns.insert(columns.end(),
                   static_projection.columns().begin(), static_projection.columns().end());
    columns.insert(columns.end(),
                   non_static_projection.columns().begin(), non_static_projection.columns().end());
  }
  rowblock->reset(new QLRowBlock(Schema(columns, 0)));
  QLRow& row = rowblock->get()->Extend();
  row.mutable_column(0)->set_bool_value(*should_apply);
  if (!*should_apply && !table_row->empty()) {
    PopulateRow(*table_row, static_projection, 1 /* begin col_idx */, &row);
    PopulateRow(*table_row, non_static_projection, 1 + static_projection.num_columns(), &row);
  }

  return Status::OK();
}

Status QLWriteOperation::Apply(const DocOperationApplyData& data) {
  bool should_apply = true;
  QLTableRow table_row;
  if (request_.has_if_expr()) {
    RETURN_NOT_OK(IsConditionSatisfied(request_.if_expr().condition(),
                                       data,
                                       &should_apply,
                                       &rowblock_,
                                       &table_row));
  } else if (RequireReadForExpressions(request_)) {
    RETURN_NOT_OK(ReadColumns(data, nullptr, nullptr, &table_row));
  }

  if (should_apply) {
    const MonoDelta ttl =
        request_.has_ttl() ? MonoDelta::FromMilliseconds(request_.ttl()) : Value::kMaxTtl;
    const UserTimeMicros user_timestamp = request_.has_user_timestamp_usec() ?
        request_.user_timestamp_usec() : Value::kInvalidUserTimestamp;

    switch (request_.type()) {
      // QL insert == update (upsert) to be consistent with Cassandra's semantics. In either
      // INSERT or UPDATE, if non-key columns are specified, they will be inserted which will cause
      // the primary key to be inserted also when necessary. Otherwise, we should insert the
      // primary key at least.
      case QLWriteRequestPB::QL_STMT_INSERT:
      case QLWriteRequestPB::QL_STMT_UPDATE: {
        // Add the appropriate liveness column only for inserts.
        // We never use init markers for QL to ensure we perform writes without any reads to
        // ensure our write path is fast while complicating the read path a bit.
        if (request_.type() == QLWriteRequestPB::QL_STMT_INSERT && pk_doc_path_ != nullptr) {
          const DocPath sub_path(pk_doc_path_->encoded_doc_key(),
                                 PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn));
          const auto value = Value(PrimitiveValue(), ttl, user_timestamp);
          RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
              sub_path, value, request_.query_id()));
        }
        if (request_.column_values_size() > 0) {
          for (const auto& column_value : request_.column_values()) {
            CHECK(column_value.has_column_id())
                << "column id missing: " << column_value.DebugString();
            const ColumnId column_id(column_value.column_id());
            const auto& column = schema_.column_by_id(column_id);
            DocPath sub_path(
                column.is_static() ?
                hashed_doc_path_->encoded_doc_key() : pk_doc_path_->encoded_doc_key(),
                PrimitiveValue(column_id));

            auto ql_type = column.type();

            WriteAction write_action = WriteAction::REPLACE; // default
            SubDocument sub_doc;
            RETURN_NOT_OK(SubDocument::FromQLExpressionPB(column_value.expr(),
                                                          column,
                                                          table_row,
                                                          &sub_doc,
                                                          &write_action));

            // Typical case, setting a columns value
            if (column_value.subscript_args().empty()) {
              switch (write_action) {
                case WriteAction::REPLACE:
                  RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
                      sub_path, sub_doc, request_.query_id(),
                      ttl, user_timestamp));
                  break;
                case WriteAction::EXTEND:
                  RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));
                  RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
                      sub_path, sub_doc, request_.query_id(), ttl));
                  break;
                case WriteAction::APPEND:
                  RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));
                  RETURN_NOT_OK(data.doc_write_batch->ExtendList(
                      sub_path, sub_doc, ListExtendOrder::APPEND,
                      request_.query_id(), ttl));
                  break;
                case WriteAction::PREPEND:
                  RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));
                  RETURN_NOT_OK(data.doc_write_batch->ExtendList(
                      sub_path, sub_doc, ListExtendOrder::PREPEND,
                      request_.query_id(), ttl));
                  break;
                case WriteAction::REMOVE_KEYS:
                  RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));
                  RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
                      sub_path, sub_doc, request_.query_id(), ttl));
                  break;
                case WriteAction::REMOVE_VALUES:
                  LOG(ERROR) << "Unsupported operation";
                  // TODO(akashnil or mihnea) this should call RemoveFromList once thats implemented
                  // Currently list subtraction is computed in memory using builtin call so this
                  // case should never be reached. Once it is implemented the corresponding case
                  // from EvalQLExpressionPB should be uncommented to enable this optimization.
                  break;
              }
            } else {
              RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));

              // Setting the value for a sub-column
              // Currently we only support two cases here: `map['key'] = v` and `list[index] = v`)
              // Any other case should be rejected by the semantic analyser before getting here
              // Later when we support frozen or nested collections this code may need refactoring
              DCHECK_EQ(column_value.subscript_args().size(), 1);
              DCHECK(write_action == WriteAction::REPLACE);

              switch (column.type()->main()) {
                case MAP: {
                  const PrimitiveValue &pv = PrimitiveValue::FromQLExpressionPB(
                      column_value.subscript_args(0), ColumnSchema::SortingType::kNotSpecified);

                  sub_path.AddSubKey(pv);
                  RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
                      sub_path, sub_doc, request_.query_id(), ttl,
                      user_timestamp));
                  break;
                }
                case LIST: {
                  MonoDelta table_ttl = schema_.table_properties().HasDefaultTimeToLive() ?
                      MonoDelta::FromMilliseconds(schema_.table_properties().DefaultTimeToLive()) :
                      MonoDelta::kMax;

                  int index = column_value.subscript_args(0).value().int32_value();
                  Status s = data.doc_write_batch->ReplaceInList(
                      sub_path, {index}, {sub_doc}, data.read_time.read, request_.query_id(),
                      table_ttl, ttl);

                  // Don't crash tserver if this is index-out-of-bounds error
                  if (s.IsQLError()) {
                    response_->set_status(QLResponsePB::YQL_STATUS_USAGE_ERROR);
                    response_->set_error_message(s.ToString());
                    return Status::OK();
                  } else if (!s.ok()) {
                    return s;
                  }

                  break;
                }
                default:
                  LOG(ERROR) << "Unexpected type for setting subcolumn: "
                             << column.type()->ToString();
              }

            }
          }
        }
        break;
      }
      case QLWriteRequestPB::QL_STMT_DELETE: {
        // We have three cases:
        // 1. If non-key columns are specified, we delete only those columns.
        // 2. Otherwise, if range cols are missing, this must be a range delete.
        // 3. Otherwise, this is a normal delete.
        // Analyzer ensures these are the only cases before getting here (e.g. range deletes cannot
        // specify non-key columns).
        if (request_.column_values_size() > 0) {
          // Delete the referenced columns only.
          for (const auto& column_value : request_.column_values()) {
            CHECK(column_value.has_column_id())
                << "column id missing: " << column_value.DebugString();
            const ColumnId column_id(column_value.column_id());
            const auto& column = schema_.column_by_id(column_id);
            const DocPath sub_path(
                column.is_static() ?
                hashed_doc_path_->encoded_doc_key() : pk_doc_path_->encoded_doc_key(),
                PrimitiveValue(column_id));
            RETURN_NOT_OK(data.doc_write_batch->DeleteSubDoc(sub_path,
                                                             request_.query_id(), user_timestamp));
          }
        } else if (IsRangeOperation(request_, schema_)) {
          // If the range columns are not specified, we read everything and delete all rows for
          // which the where condition matches.

          // Create the schema projection -- range deletes cannot reference non-primary key columns,
          // so the non-static projection is all we need, it should contain all referenced columns.
          Schema static_projection;
          Schema projection;
          RETURN_NOT_OK(CreateProjections(schema_, request_.column_refs(),
              &static_projection, &projection));

          // Construct the scan spec basing on the WHERE condition.
          vector<PrimitiveValue> hashed_components;
          RETURN_NOT_OK(QLKeyColumnValuesToPrimitiveValues(
              request_.hashed_column_values(), schema_, 0,
              schema_.num_hash_key_columns(), &hashed_components));

          DocQLScanSpec spec(projection, request_.hash_code(), -1, hashed_components,
              request_.has_where_expr() ? &request_.where_expr().condition() : nullptr,
              request_.query_id());

          // Create iterator.
          DocRowwiseIterator iterator(projection, schema_, txn_op_context_,
                                      data.doc_write_batch->rocksdb(), data.read_time);
          RETURN_NOT_OK(iterator.Init(spec));

          // Iterate through rows and delete those that match the condition.
          // TODO We do not lock here, so other write transactions coming in might appear partially
          // applied if they happen in the middle of a ranged delete.
          QLTableRow row;
          while (iterator.HasNext()) {
            row.clear();
            RETURN_NOT_OK(iterator.NextRow(projection, &row));

            // Match the row with the where condition before deleting it.
            bool match = false;
            RETURN_NOT_OK(spec.Match(row, &match));
            if (match) {
              DocKey row_key = iterator.row_key();
              DocPath row_path(row_key.Encode());
              RETURN_NOT_OK(DeleteRow(data.doc_write_batch, row_path));
            }
          }
          data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
        } else {
          // Otherwise, delete the referenced row (all columns).
          RETURN_NOT_OK(DeleteRow(data.doc_write_batch, *pk_doc_path_));
        }
        break;
      }
    }
  }

  response_->set_status(QLResponsePB::YQL_STATUS_OK);

  return Status::OK();
}

Status QLWriteOperation::DeleteRow(DocWriteBatch* doc_write_batch,
                                   const DocPath row_path) {
  if (request_.has_user_timestamp_usec()) {
    // If user_timestamp is provided, we need to add a tombstone for each individual
    // column in the schema since we don't want to analyze this on the read path.
    for (int i = schema_.num_key_columns(); i < schema_.num_columns(); i++) {
      const DocPath sub_path(row_path.encoded_doc_key(),
                             PrimitiveValue(schema_.column_id(i)));
      RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(sub_path,
                                                  request_.query_id(),
                                                  request_.user_timestamp_usec()));
    }

    // Delete the liveness column as well.
    const DocPath liveness_column(
        row_path.encoded_doc_key(),
        PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn));
    RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(liveness_column,
                                                request_.query_id(),
                                                request_.user_timestamp_usec()));
  } else {
    RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(row_path));
  }

  return Status::OK();
}

Status QLReadOperation::Execute(const common::QLStorageIf& ql_storage,
                                const ReadHybridTime& read_time,
                                const Schema& schema,
                                const Schema& query_schema,
                                QLResultSet* resultset,
                                HybridTime* restart_read_ht) {
  size_t row_count_limit = std::numeric_limits<std::size_t>::max();
  if (request_.has_limit()) {
    if (request_.limit() == 0) {
      return Status::OK();
    }
    row_count_limit = request_.limit();
  }

  // Create the projections of the non-key columns selected by the row block plus any referenced in
  // the WHERE condition. When DocRowwiseIterator::NextRow() populates the value map, it uses this
  // projection only to scan sub-documents. The query schema is used to select only referenced
  // columns and key columns.
  Schema static_projection, non_static_projection;
  RETURN_NOT_OK(CreateProjections(schema, request_.column_refs(),
                                  &static_projection, &non_static_projection));
  const bool read_static_columns = !static_projection.columns().empty();
  const bool read_distinct_columns = request_.distinct();

  std::unique_ptr<common::QLRowwiseIteratorIf> iter;
  std::unique_ptr<common::QLScanSpec> spec, static_row_spec;
  ReadHybridTime req_read_time;
  RETURN_NOT_OK(ql_storage.BuildQLScanSpec(
      request_, read_time, schema, read_static_columns, static_projection, &spec,
      &static_row_spec, &req_read_time));
  RETURN_NOT_OK(ql_storage.GetIterator(request_, query_schema, schema, txn_op_context_,
                                       req_read_time, &iter));
  RETURN_NOT_OK(iter->Init(*spec));
  if (FLAGS_trace_docdb_calls) {
    TRACE("Initialized iterator");
  }
  QLTableRow static_row, non_static_row;
  QLTableRow& selected_row = read_distinct_columns ? static_row : non_static_row;

  // In case when we are continuing a select with a paging state, the static columns for the next
  // row to fetch are not included in the first iterator and we need to fetch them with a separate
  // spec and iterator before beginning the normal fetch below.
  if (static_row_spec != nullptr) {
    std::unique_ptr<common::QLRowwiseIteratorIf> static_row_iter;
    RETURN_NOT_OK(ql_storage.GetIterator(request_, static_projection, schema, txn_op_context_,
                                         req_read_time, &static_row_iter));
    RETURN_NOT_OK(static_row_iter->Init(*static_row_spec));
    if (static_row_iter->HasNext()) {
      RETURN_NOT_OK(static_row_iter->NextRow(static_projection, &static_row));
    }
  }

  // Begin the normal fetch.
  while (resultset->rsrow_count() < row_count_limit && iter->HasNext()) {

    // Note that static columns are sorted before non-static columns in DocDB as follows. This is
    // because "<empty_range_components>" is empty and terminated by kGroupEnd which sorts before
    // all other ValueType characters in a non-empty range component.
    //   <hash_code><hash_components><empty_range_components><static_column_id> -> value;
    //   <hash_code><hash_components><range_components><non_static_column_id> -> value;
    if (iter->IsNextStaticColumn()) {

      // If the next row is a row that contains a static column, read it if the select list contains
      // a static column. Otherwise, skip this row and continue to read the next row.
      if (read_static_columns) {
        static_row.clear();
        RETURN_NOT_OK(iter->NextRow(static_projection, &static_row));

        // If we are not selecting distinct columns (i.e. hash and static columns only), continue
        // to scan for the non-static (regular) row.
        if (!read_distinct_columns) {
          continue;
        }

      } else {
        iter->SkipRow();
        continue;
      }

    } else { // Reading a regular row that contains non-static columns.

      // If we are selecting distinct columns (which means hash and static columns only), skip this
      // row and continue to read next row.
      if (read_distinct_columns) {
        iter->SkipRow();
        continue;
      }

      // Read this regular row.
      non_static_row.clear();
      RETURN_NOT_OK(iter->NextRow(non_static_projection, &non_static_row));

      // If select list contains static columns and we have read a row that contains the static
      // columns for the same hash key, copy the static columns into this row.
      if (read_static_columns) {
        JoinStaticRow(schema, static_projection, static_row, &non_static_row);
      }
    }

    // Match the row with the where condition before adding to the row block.
    bool match = false;
    RETURN_NOT_OK(spec->Match(selected_row, &match));
    if (match) {
      RETURN_NOT_OK(PopulateResultSet(selected_row, resultset));
    }
  }
  if (FLAGS_trace_docdb_calls) {
    TRACE("Fetched $0 rows.", resultset->rsrow_count());
  }
  *restart_read_ht = iter->RestartReadHt();

  if (resultset->rsrow_count() >= row_count_limit) {
    RETURN_NOT_OK(iter->SetPagingStateIfNecessary(request_, &response_));
  }

  return Status::OK();
}

CHECKED_STATUS QLReadOperation::PopulateResultSet(const QLTableRow& table_row,
                                                  QLResultSet *resultset) {
  DocExprExecutor executor;
  int column_count = request_.selected_exprs().size();
  QLRSRow *rsrow = resultset->AllocateRSRow(column_count);

  int rscol_index = 0;
  for (const QLExpressionPB& expr : request_.selected_exprs()) {
    RETURN_NOT_OK(executor.EvalExpr(expr, table_row, rsrow->rscol(rscol_index)));
    rscol_index++;
  }

  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
