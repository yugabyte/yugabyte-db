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

Status KuduWriteOperation::Apply(
    DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) {
  return doc_write_batch->SetPrimitive(doc_path_, value_, InitMarkerBehavior::OPTIONAL);
}

void RedisWriteOperation::GetDocPathsToLock(list<DocPath> *paths, IsolationLevel *level) const {
  paths->push_back(DocPath::DocPathFromRedisKey(request_.key_value().hash_code(),
                                                request_.key_value().key()));
  *level = IsolationLevel::SNAPSHOT_ISOLATION;
}

namespace {

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

Status GetRedisValueType(
    rocksdb::DB* rocksdb,
    const HybridTime &hybrid_time,
    const RedisKeyValuePB &key_value_pb,
    RedisDataType *type,
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
    doc = SubDocument(cached_entry->second);
  } else {
    RETURN_NOT_OK(GetSubDocument(
        rocksdb, subdoc_key, &doc, &doc_found, rocksdb::kDefaultQueryId,
        hybrid_time, Value::kMaxTtl, /* return_type_only */ true));
  }

  if (!doc_found) {
    *type = REDIS_TYPE_NONE;
    return Status::OK();
  }

  switch (doc.value_type()) {
    case ValueType::kInvalidValueType: FALLTHROUGH_INTENDED;
    case ValueType::kTombstone:
      *type = REDIS_TYPE_NONE;
      return Status::OK();
    case ValueType::kObject:
      *type = REDIS_TYPE_HASH;
      return Status::OK();
    case ValueType::kRedisSet:
      *type = REDIS_TYPE_SET;
      return Status::OK();
    case ValueType::kRedisTS:
      *type = REDIS_TYPE_TIMESERIES;
      return Status::OK();
    case ValueType::kNull: FALLTHROUGH_INTENDED; // This value is a set member.
    case ValueType::kString:
      *type = REDIS_TYPE_STRING;
      return Status::OK();
    default:
      return Status::OK();
  }
}

CHECKED_STATUS GetRedisValue(
    rocksdb::DB *rocksdb,
    HybridTime hybrid_time,
    const RedisKeyValuePB &key_value_pb,
    RedisDataType *type,
    string *value,
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

  RETURN_NOT_OK(GetSubDocument(rocksdb, doc_key, &doc, &doc_found,
                               rocksdb::kDefaultQueryId, hybrid_time));

  if (!doc_found) {
    *type = REDIS_TYPE_NONE;
    *value = "";
    return Status::OK();
  }

  if (!doc.IsPrimitive()) {
    switch (doc.value_type()) {
      case ValueType::kObject:
        *type = REDIS_TYPE_HASH;
        break;
      case ValueType::kRedisTS:
        *type = REDIS_TYPE_TIMESERIES;
        break;
      case ValueType::kRedisSet:
        *type = REDIS_TYPE_SET;
        break;
      default:
        return STATUS_SUBSTITUTE(IllegalState, "Invalid value type: $0",
                                 static_cast<int>(doc.value_type()));
    }
    return Status::OK();
  }

  *type = REDIS_TYPE_STRING;
  *value = doc.GetString();
  return Status::OK();
}

// Set response based on the type match. Return whether the type matches what's expected.
bool VerifyTypeAndSetCode(
    const RedisDataType expected_type,
    const RedisDataType actual_type,
    RedisResponsePB *response,
    bool verify_success_if_missing = false) {
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

} // anonymous namespace

Status RedisWriteOperation::Apply(
    DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) {
  switch (request_.request_case()) {
    case RedisWriteRequestPB::RequestCase::kSetRequest:
      return ApplySet(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kGetsetRequest:
      return ApplyGetSet(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kAppendRequest:
      return ApplyAppend(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kDelRequest:
      return ApplyDel(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kSetRangeRequest:
      return ApplySetRange(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kIncrRequest:
      return ApplyIncr(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kPushRequest:
      return ApplyPush(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kInsertRequest:
      return ApplyInsert(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kPopRequest:
      return ApplyPop(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kAddRequest:
      return ApplyAdd(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::kRemoveRequest:
      return ApplyRemove(doc_write_batch);
    case RedisWriteRequestPB::RequestCase::REQUEST_NOT_SET: break;
  }
  return STATUS(Corruption,
      Substitute("Unsupported redis read operation: $0", request_.request_case()));
}

Status RedisWriteOperation::ApplySet(DocWriteBatch* doc_write_batch) {
  const RedisKeyValuePB& kv = request_.key_value();
  RedisDataType data_type;
  RETURN_NOT_OK(GetRedisValueType(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &data_type,
                                  doc_write_batch));

  const MonoDelta ttl = request_.set_request().has_ttl() ?
      MonoDelta::FromMilliseconds(request_.set_request().ttl()) : Value::kMaxTtl;
  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
  if (kv.subkey_size() > 0) {
    switch (kv.type()) {
      case REDIS_TYPE_TIMESERIES: FALLTHROUGH_INTENDED;
      case REDIS_TYPE_HASH: {
        if (data_type != kv.type() && data_type != REDIS_TYPE_NONE) {
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

        if (kv.subkey_size() == 1 && FLAGS_emulate_redis_responses) {
          RedisDataType type;
          RETURN_NOT_OK(GetRedisValueType(
              doc_write_batch->rocksdb(), read_hybrid_time_, kv, &type, doc_write_batch, 0));
          // For HSET/TSADD, we return 0 or 1 depending on if the key already existed.
          // If flag is false, no int response is returned.
          response_.set_int_response(type == REDIS_TYPE_NONE ? 1 : 0);
        }
        if (data_type == REDIS_TYPE_NONE && kv.type() == REDIS_TYPE_TIMESERIES) {
          // Need to insert the document instead of extending it.
          RETURN_NOT_OK(doc_write_batch->InsertSubDocument(
              doc_path, kv_entries, InitMarkerBehavior::REQUIRED, ttl));
        } else {
          RETURN_NOT_OK(doc_write_batch->ExtendSubDocument(
              doc_path, kv_entries, InitMarkerBehavior::REQUIRED, ttl));
        }
        break;
      }
      case REDIS_TYPE_STRING: {
        if (data_type != REDIS_TYPE_STRING && data_type != REDIS_TYPE_NONE) {
          response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
          return Status::OK();
        }
        for (int i = 0; i < kv.subkey_size(); i++) {
          DocPath subdoc_path = doc_path;
          subdoc_path.AddSubKey(PrimitiveValue(kv.subkey(i).string_subkey()));
          RETURN_NOT_OK(doc_write_batch->SetPrimitive(
              subdoc_path, Value(PrimitiveValue(kv.value(i)), ttl)));
        }
        break;
      }
      default:
        return STATUS_SUBSTITUTE(InvalidCommand,
            "Redis data type $0 not supported in SET command", kv.type());
    }
  } else {
    if (kv.type() != REDIS_TYPE_STRING && kv.type() != REDIS_TYPE_NONE) {
      return STATUS_SUBSTITUTE(InvalidCommand,
          "Redis data type for SET must be string if subkey not present, found $0", kv.type());
    }
    if (kv.value_size() != 1) {
      return STATUS_SUBSTITUTE(InvalidCommand,
          "There must be only one value in SET if there is only one key, found $0",
          kv.value_size());
    }
        RETURN_NOT_OK(doc_write_batch->SetPrimitive(doc_path,
        Value(PrimitiveValue(kv.value(0)), ttl)));
  }
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

Status RedisWriteOperation::ApplyGetSet(DocWriteBatch* doc_write_batch) {
  RedisDataType type;
  string value;
  const RedisKeyValuePB& kv = request_.key_value();

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &type, &value));

  if (kv.value_size() != 1) {
    return STATUS_SUBSTITUTE(Corruption,
        "Getset kv should have 1 value, found $0", kv.value_size());
  }

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }
  response_.set_string_response(value);

  return doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(kv.value(0))));
}

Status RedisWriteOperation::ApplyAppend(DocWriteBatch* doc_write_batch) {
  RedisDataType type;
  string value;
  const RedisKeyValuePB& kv = request_.key_value();

  if (kv.value_size() != 1) {
    return STATUS_SUBSTITUTE(Corruption,
        "Append kv should have 1 value, found $0", kv.value_size());
  }

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &type, &value));

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, type, &response_, true)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  const string& new_value = value + kv.value(0);

  response_.set_int_response(new_value.length());

  return doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()), Value(PrimitiveValue(new_value)));
}

// TODO (akashnil): Actually check if the value existed, return 0 if not. handle multidel in future.
//                  See ENG-807
Status RedisWriteOperation::ApplyDel(DocWriteBatch* doc_write_batch) {
  const RedisKeyValuePB& kv = request_.key_value();
  RedisDataType data_type;
  RETURN_NOT_OK(GetRedisValueType(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &data_type,
                                  doc_write_batch));
  if (data_type != REDIS_TYPE_NONE && data_type != kv.type() && kv.type() != REDIS_TYPE_NONE) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    return Status::OK();
  }
  SubDocument values =  SubDocument();
  int num_keys;
  if (kv.type() == REDIS_TYPE_NONE) { // Delete any string, or container.
    values = SubDocument(ValueType::kTombstone);
    // Currently we only support deleting one key with DEL command.
    num_keys = data_type == REDIS_TYPE_NONE ? 0 : 1;
  } else {
    num_keys = kv.subkey_size(); // We know the subkeys are distinct.
    if (FLAGS_emulate_redis_responses) {
      for (int i = 0; i < kv.subkey_size(); i++) {
        RedisDataType type;
        RETURN_NOT_OK(GetRedisValueType(
            doc_write_batch->rocksdb(), read_hybrid_time_, kv, &type, doc_write_batch, i));
        if (type == REDIS_TYPE_STRING) {
          values.SetChild(PrimitiveValue(kv.subkey(i).string_subkey()),
                          SubDocument(ValueType::kTombstone));
        } else {
          // If the key is absent, it doesn't contribute to the count of keys being deleted.
          num_keys--;
        }
      }
    } else {
      for (int i = 0; i < kv.subkey_size(); i++) {
        values.SetChild(PrimitiveValue(kv.subkey(i).string_subkey()),
                        SubDocument(ValueType::kTombstone));
      }
    }
  }
  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
  RETURN_NOT_OK(doc_write_batch->ExtendSubDocument(doc_path, values, InitMarkerBehavior::REQUIRED));
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  if (FLAGS_emulate_redis_responses) {
    // If the flag is true, we respond with the number of keys actually being deleted.
    response_.set_int_response(num_keys);
  }
  return Status::OK();
}

Status RedisWriteOperation::ApplySetRange(DocWriteBatch* doc_write_batch) {
  RedisDataType type;
  string value;
  const RedisKeyValuePB& kv = request_.key_value();
  if (kv.value_size() != 1) {
    return STATUS_SUBSTITUTE(Corruption,
        "SetRange kv should have 1 value, found $0", kv.value_size());
  }

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &type, &value));

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, type, &response_, true)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  // TODO (akashnil): Handle overflows.
  if (request_.set_range_request().offset() > value.length()) {
    value.resize(request_.set_range_request().offset(), 0);
  }
  value.replace(request_.set_range_request().offset(), kv.value(0).length(), kv.value(0));
  response_.set_int_response(value.length());

  return doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(value)));
}

Status RedisWriteOperation::ApplyIncr(DocWriteBatch* doc_write_batch, int64_t incr) {
  RedisDataType type;
  string value;
  const RedisKeyValuePB& kv = request_.key_value();

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &type, &value));

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  int64_t old_value, new_value;

  try {
    old_value = std::stoll(value);
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

  return doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(std::to_string(new_value))));
}

Status RedisWriteOperation::ApplyPush(DocWriteBatch* doc_write_batch) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

Status RedisWriteOperation::ApplyInsert(DocWriteBatch* doc_write_batch) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

Status RedisWriteOperation::ApplyPop(DocWriteBatch* doc_write_batch) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

Status RedisWriteOperation::ApplyAdd(DocWriteBatch* doc_write_batch) {
  const RedisKeyValuePB& kv = request_.key_value();

  RedisDataType data_type;
  RETURN_NOT_OK(GetRedisValueType(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &data_type,
                                  doc_write_batch));

  if (data_type != REDIS_TYPE_SET && data_type != REDIS_TYPE_NONE) {
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
      RedisDataType type;
      string value;
      RETURN_NOT_OK(GetRedisValueType(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &type,
                                      doc_write_batch, i));
      if (type != REDIS_TYPE_NONE) {
        num_keys_found++;
      }
    }

    set_entries.SetChild(
        PrimitiveValue(kv.subkey(i).string_subkey()),
        SubDocument(PrimitiveValue(ValueType::kNull)));
  }

  RETURN_NOT_OK(set_entries.ConvertToRedisSet());

  Status s;

  if (data_type == REDIS_TYPE_NONE) {
    RETURN_NOT_OK(
        doc_write_batch->InsertSubDocument(doc_path, set_entries, InitMarkerBehavior::REQUIRED));
  } else {
    RETURN_NOT_OK(
        doc_write_batch->ExtendSubDocument(doc_path, set_entries, InitMarkerBehavior::REQUIRED));
  }

  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  if (FLAGS_emulate_redis_responses) {
    // If flag is set, the actual number of new keys added is sent as response.
    response_.set_int_response(kv.subkey_size() - num_keys_found);
  }
  return Status::OK();
}

Status RedisWriteOperation::ApplyRemove(DocWriteBatch* doc_write_batch) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

const RedisResponsePB& RedisWriteOperation::response() { return response_; }

Status RedisReadOperation::Execute(rocksdb::DB *rocksdb, const HybridTime& hybrid_time) {
  switch (request_.request_case()) {
    case RedisReadRequestPB::RequestCase::kGetRequest:
      return ExecuteGet(rocksdb, hybrid_time);
    case RedisReadRequestPB::RequestCase::kStrlenRequest:
      return ExecuteStrLen(rocksdb, hybrid_time);
    case RedisReadRequestPB::RequestCase::kExistsRequest:
      return ExecuteExists(rocksdb, hybrid_time);
    case RedisReadRequestPB::RequestCase::kGetRangeRequest:
      return ExecuteGetRange(rocksdb, hybrid_time);
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

void PopulateResponseFrom(const SubDocument::ObjectContainer &key_values,
                          RedisResponsePB *response,
                          bool add_keys,
                          bool add_values) {
  for (auto iter = key_values.begin(); iter != key_values.end(); iter++) {
    const PrimitiveValue& first = iter->first;
    const PrimitiveValue& second = iter->second;
    if (add_keys) response->mutable_array_response()->add_elements(first.GetString());
    if (add_values) response->mutable_array_response()->add_elements(second.GetString());
  }
}

Status RedisReadOperation::ExecuteHGetAllLikeCommands(rocksdb::DB *rocksdb,
                                                      HybridTime hybrid_time,
                                                      ValueType value_type,
                                                      bool add_keys,
                                                      bool add_values) {
  SubDocKey doc_key(
      DocKey::FromRedisKey(request_.key_value().hash_code(), request_.key_value().key()));
  SubDocument doc;
  bool doc_found = false;
  RETURN_NOT_OK(GetSubDocument(
  rocksdb, doc_key, &doc, &doc_found, rocksdb::kDefaultQueryId, hybrid_time));
  if (add_keys || add_values) {
    response_.set_allocated_array_response(new RedisArrayPB());
  }
  if (!doc_found) {
    response_.set_code(RedisResponsePB_RedisStatusCode_OK);
    return Status::OK();
  }
  if (VerifyTypeAndSetCode(value_type, doc.value_type(), &response_)) {
    if (add_keys || add_values) {
      PopulateResponseFrom(doc.object_container(), &response_, add_keys, add_values);
    } else {
      response_.set_int_response(doc.object_container().size());
    }
  }
  return Status::OK();
}

Status RedisReadOperation::ExecuteGet(rocksdb::DB *rocksdb, HybridTime hybrid_time) {

  RedisDataType type;

  const auto request_type = request_.get_request().request_type();
  switch (request_type) {
    case RedisGetRequestPB_GetRequestType_GET: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_TSGET: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_HGET: {
      string value;
      RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));

      // If wrong type, we set the error code in the response.
      if (VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
        response_.set_string_response(value);
      }
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_HEXISTS: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_SISMEMBER: {
      RETURN_NOT_OK(GetRedisValueType(rocksdb, hybrid_time, request_.key_value(), &type, nullptr,
                                      -1));
      if (VerifyTypeAndSetCode(
          (request_type == RedisGetRequestPB_GetRequestType_HEXISTS
              ? RedisDataType::REDIS_TYPE_HASH
              : RedisDataType::REDIS_TYPE_SET),
          type, &response_, true)) {
        RETURN_NOT_OK(GetRedisValueType(rocksdb, hybrid_time, request_.key_value(), &type,
                                        nullptr, 0));
        response_.set_int_response((type == RedisDataType::REDIS_TYPE_NONE) ? 0 : 1);
      }
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_HSTRLEN: {
      RETURN_NOT_OK(GetRedisValueType(rocksdb, hybrid_time, request_.key_value(), &type,
                                      nullptr, -1));
      if (VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_HASH, type, &response_, true)) {
        string value;
        RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));
        response_.set_int_response((type == RedisDataType::REDIS_TYPE_NONE) ? 0 : value.length());
      }
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_MGET: {
      return STATUS(NotSupported, "MGET not yet supported");
    }
    case RedisGetRequestPB_GetRequestType_HMGET: {
      RETURN_NOT_OK(GetRedisValueType(rocksdb, hybrid_time, request_.key_value(), &type, nullptr));
      if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_HASH, type, &response_, true)) {
          return Status::OK();
      }

      response_.set_allocated_array_response(new RedisArrayPB());
      for (int i = 0; i < request_.key_value().subkey_size(); i++) {
        // TODO: ENG-1803: It is inefficient to create a new iterator for each subkey causing a
        // new seek. Consider reusing the same iterator.
        string value;
        RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value, i));
        if (type == REDIS_TYPE_STRING) {
          response_.mutable_array_response()->add_elements(value);
        } else {
          response_.mutable_array_response()->add_elements(""); // Empty is nil response.
        }
      }
      response_.set_code(RedisResponsePB_RedisStatusCode_OK);
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_HGETALL:
      return ExecuteHGetAllLikeCommands(rocksdb, hybrid_time, ValueType::kObject, true, true);
    case RedisGetRequestPB_GetRequestType_HKEYS:
      return ExecuteHGetAllLikeCommands(rocksdb, hybrid_time, ValueType::kObject, true, false);
    case RedisGetRequestPB_GetRequestType_HVALS:
      return ExecuteHGetAllLikeCommands(rocksdb, hybrid_time, ValueType::kObject, false, true);
    case RedisGetRequestPB_GetRequestType_HLEN:
      return ExecuteHGetAllLikeCommands(rocksdb, hybrid_time, ValueType::kObject, false, false);
    case RedisGetRequestPB_GetRequestType_SMEMBERS:
      return ExecuteHGetAllLikeCommands(rocksdb, hybrid_time, ValueType::kRedisSet, true, false);
    case RedisGetRequestPB_GetRequestType_SCARD:
      return ExecuteHGetAllLikeCommands(rocksdb, hybrid_time, ValueType::kRedisSet, false, false);
    case RedisGetRequestPB_GetRequestType_UNKNOWN: {
      return STATUS(InvalidCommand, "Unknown Get Request not supported");
    }
  }
  return Status::OK();
}

Status RedisReadOperation::ExecuteStrLen(rocksdb::DB *rocksdb, HybridTime hybrid_time) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));

  if (VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, type, &response_, true)) {
    response_.set_int_response((type == RedisDataType::REDIS_TYPE_NONE) ? 0 : value.length());
  }

  return Status::OK();
}

Status RedisReadOperation::ExecuteExists(rocksdb::DB *rocksdb, HybridTime hybrid_time) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));

  if (type != REDIS_TYPE_NONE) {
    response_.set_code(RedisResponsePB_RedisStatusCode_OK);
    // We only support exist command with one argument currently.
    response_.set_int_response(1);
  } else {
    response_.set_code(RedisResponsePB_RedisStatusCode_OK);
    response_.set_int_response(0);
  }
  return Status::OK();
}

Status RedisReadOperation::ExecuteGetRange(rocksdb::DB *rocksdb, HybridTime hybrid_time) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  const int32_t len = value.length();

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

  response_.set_string_response(value.substr(start, end - start + 1));
  return Status::OK();
}

const RedisResponsePB& RedisReadOperation::response() {
  return response_;
}

namespace {

bool RequireRead(const QLWriteRequestPB& request) {
  // A QLWriteOperation requires a read if it contains an IF clause or an UPDATE assignment that
  // involves an expresion with a column reference. If the IF clause contains a condition that
  // involves a column reference, the column will be included in "column_refs". However, we cannot
  // rely on non-empty "column_ref" alone to decide if a read is required becaue "IF EXISTS" and
  // "IF NOT EXISTS" do not involve a column reference explicitly.
  return request.has_if_expr()
      || request.has_column_refs() && (!request.column_refs().ids().empty() ||
                                       !request.column_refs().static_ids().empty());
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
    QLWriteRequestPB* request, const Schema& schema, QLResponsePB* response)
    : schema_(schema), response_(response), require_read_(RequireRead(*request)) {
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

  // We need the hashed key if writing to the static columns, and need primary key if writing to
  // non-static columns or writing the full primary key (i.e. range columns are present or table
  // does not have range columns).
  CHECK_OK(InitializeKeys(
      write_static_columns,
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

Status QLWriteOperation::ReadColumns(rocksdb::DB *rocksdb,
                                      const HybridTime& hybrid_time,
                                      Schema *param_static_projection,
                                      Schema *param_non_static_projection,
                                      QLTableRow *table_row,
                                      const rocksdb::QueryId query_id) {

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
    DocQLScanSpec spec(*static_projection, *hashed_doc_key_, query_id);
    DocRowwiseIterator iterator(*static_projection, schema_, rocksdb, hybrid_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(*static_projection, table_row));
    }
  }
  if (pk_doc_key_ != nullptr) {
    DocQLScanSpec spec(*non_static_projection, *pk_doc_key_, query_id);
    DocRowwiseIterator iterator(*non_static_projection, schema_, rocksdb, hybrid_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(*non_static_projection, table_row));
    } else {
      // If no non-static column is found, the row does not exist and we should clear the static
      // columns in the map to indicate the row does not exist.
      table_row->clear();
    }
  }

  return Status::OK();
}

Status QLWriteOperation::IsConditionSatisfied(const QLConditionPB& condition,
                                               rocksdb::DB *rocksdb,
                                               const HybridTime& hybrid_time,
                                               bool* should_apply,
                                               std::unique_ptr<QLRowBlock>* rowblock,
                                               QLTableRow* table_row,
                                               const rocksdb::QueryId query_id) {

  // Read column values.
  Schema static_projection, non_static_projection;
  RETURN_NOT_OK(ReadColumns(rocksdb, hybrid_time, &static_projection,
                            &non_static_projection, table_row, query_id));

  // See if the if-condition is satisfied.
  RETURN_NOT_OK(EvaluateCondition(condition, *table_row, should_apply));

  // Populate the result set to return the "applied" status, and optionally the present column
  // values if the condition is not satisfied and the row does exist (value_map is not empty).
  vector<ColumnSchema> columns;
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

Status QLWriteOperation::Apply(
    DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) {

  bool should_apply = true;
  QLTableRow table_row;
  if (request_.has_if_expr()) {
    RETURN_NOT_OK(IsConditionSatisfied(request_.if_expr().condition(),
                                       rocksdb,
                                       hybrid_time,
                                       &should_apply,
                                       &rowblock_,
                                       &table_row,
                                       request_.query_id()));
  } else if (require_read_) {
    RETURN_NOT_OK(ReadColumns(rocksdb, hybrid_time, nullptr, nullptr, &table_row,
                              request_.query_id()));
  }

  if (should_apply) {
    const MonoDelta ttl =
        request_.has_ttl() ? MonoDelta::FromMilliseconds(request_.ttl()) : Value::kMaxTtl;

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
          const auto value = Value(PrimitiveValue(), ttl);
          RETURN_NOT_OK(doc_write_batch->SetPrimitive(sub_path, value,
              InitMarkerBehavior::OPTIONAL));
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
                  RETURN_NOT_OK(doc_write_batch->InsertSubDocument(sub_path,
                                                                   sub_doc,
                                                                   InitMarkerBehavior::OPTIONAL,
                                                                   ttl));
                  break;
                case WriteAction::EXTEND:
                  RETURN_NOT_OK(doc_write_batch->ExtendSubDocument(sub_path,
                                                                   sub_doc,
                                                                   InitMarkerBehavior::OPTIONAL,
                                                                   ttl));
                  break;
                case WriteAction::APPEND:
                  RETURN_NOT_OK(doc_write_batch->ExtendList(sub_path,
                                                            sub_doc,
                                                            ListExtendOrder::APPEND,
                                                            InitMarkerBehavior::OPTIONAL,
                                                            ttl));
                  break;
                case WriteAction::PREPEND:
                  RETURN_NOT_OK(doc_write_batch->ExtendList(sub_path,
                                                            sub_doc,
                                                            ListExtendOrder::PREPEND,
                                                            InitMarkerBehavior::OPTIONAL,
                                                            ttl));
                  break;
                case WriteAction::REMOVE_KEYS:
                  RETURN_NOT_OK(doc_write_batch->ExtendSubDocument(sub_path,
                                                                   sub_doc,
                                                                   InitMarkerBehavior::OPTIONAL,
                                                                   ttl));
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
                  RETURN_NOT_OK(doc_write_batch->InsertSubDocument(sub_path,
                                                                   sub_doc,
                                                                   InitMarkerBehavior::OPTIONAL,
                                                                   ttl));
                  break;
                }
                case LIST: {
                  MonoDelta table_ttl = schema_.table_properties().HasDefaultTimeToLive() ?
                      MonoDelta::FromMilliseconds(schema_.table_properties().DefaultTimeToLive()) :
                      MonoDelta::kMax;

                  int index = column_value.subscript_args(0).value().int32_value();
                  Status s = doc_write_batch->ReplaceInList(sub_path, {index}, {sub_doc},
                      hybrid_time,
                      request_.query_id(),
                      table_ttl,
                      ttl,
                      InitMarkerBehavior::OPTIONAL);

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
        // If non-key columns are specified, just the individual columns will be deleted. Otherwise,
        // the whole row is deleted.
        if (request_.column_values_size() > 0) {
          for (const auto& column_value : request_.column_values()) {
            CHECK(column_value.has_column_id())
                << "column id missing: " << column_value.DebugString();
            const ColumnId column_id(column_value.column_id());
            const auto& column = schema_.column_by_id(column_id);
            const DocPath sub_path(
                column.is_static() ?
                hashed_doc_path_->encoded_doc_key() : pk_doc_path_->encoded_doc_key(),
                PrimitiveValue(column_id));
            RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(sub_path, InitMarkerBehavior::OPTIONAL));
          }
        } else {
          RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(
              *pk_doc_path_, InitMarkerBehavior::OPTIONAL));
        }
        break;
      }
    }

  }

  response_->set_status(QLResponsePB::YQL_STATUS_OK);

  return Status::OK();
}

Status QLReadOperation::Execute(const common::QLStorageIf& ql_storage,
                                const HybridTime& hybrid_time,
                                const Schema& schema,
                                const Schema& query_schema,
                                QLResultSet* resultset) {
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
  HybridTime req_hybrid_time;
  RETURN_NOT_OK(ql_storage.BuildQLScanSpec(request_, hybrid_time, schema, read_static_columns,
                                             static_projection, &spec, &static_row_spec,
                                             &req_hybrid_time));
  RETURN_NOT_OK(ql_storage.GetIterator(request_, query_schema, schema, req_hybrid_time, &iter));
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
    RETURN_NOT_OK(ql_storage.GetIterator(request_, static_projection, schema, req_hybrid_time,
                                          &static_row_iter));
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

const QLResponsePB& QLReadOperation::response() const { return response_; }

}  // namespace docdb
}  // namespace yb
