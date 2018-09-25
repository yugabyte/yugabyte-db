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

#include "yb/docdb/doc_operation.h"

#include "yb/common/jsonb.h"
#include "yb/common/partition.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/ql_storage_interface.h"
#include "yb/common/ql_value.h"

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/doc_expr.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/subdocument.h"

#include "yb/server/hybrid_clock.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/stol_utils.h"
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
using std::make_shared;
using std::unique_ptr;
using std::make_unique;
using strings::Substitute;
using common::Jsonb;
using yb::bfql::TSOpcode;

//--------------------------------------------------------------------------------------------------
// Redis support.
//--------------------------------------------------------------------------------------------------

// A simple conversion from RedisDataTypes to ValueTypes
// Note: May run into issues if we want to support ttl on individual set elements,
// as they are represented by ValueType::kNull.
ValueType ValueTypeFromRedisType(RedisDataType dt) {
  switch(dt) {
  case RedisDataType::REDIS_TYPE_STRING:
    return ValueType::kString;
  case RedisDataType::REDIS_TYPE_SET:
    return ValueType::kRedisSet;
  case RedisDataType::REDIS_TYPE_HASH:
    return ValueType::kObject;
  case RedisDataType::REDIS_TYPE_SORTEDSET:
    return ValueType::kRedisSortedSet;
  case RedisDataType::REDIS_TYPE_TIMESERIES:
    return ValueType::kRedisTS;
  case RedisDataType::REDIS_TYPE_LIST:
    return ValueType::kRedisList;
  default:
    return ValueType::kInvalid;
  }
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

static const string wrong_type_message =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

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
    case RedisKeyValueSubKeyPB::SubkeyCase::kDoubleSubkey: {
      *primitive_value = PrimitiveValue::Double(subkey_pb.double_subkey());
      break;
    }
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
    case REDIS_TYPE_SORTEDSET:
      if (!subkey_pb.has_double_subkey()) {
        return STATUS_SUBSTITUTE(InvalidArgument, "subkey: $0 should be of double type",
                             subkey_pb.ShortDebugString());
      }
      break;
    default:
      return STATUS_SUBSTITUTE(IllegalState, "Invalid enum value $0", data_type);
  }
  return PrimitiveValueFromSubKey(subkey_pb, primitive_value);
}

Result<RedisDataType> GetRedisValueType(
    IntentAwareIterator* iterator,
    const RedisKeyValuePB &key_value_pb,
    DocWriteBatch* doc_write_batch = nullptr,
    int subkey_index = kNilSubkeyIndex,
    bool always_override = false) {
  if (!key_value_pb.has_key()) {
    return STATUS(Corruption, "Expected KeyValuePB");
  }
  KeyBytes encoded_subdoc_key;
  if (subkey_index == kNilSubkeyIndex) {
    encoded_subdoc_key = DocKey::EncodedFromRedisKey(key_value_pb.hash_code(), key_value_pb.key());
  } else {
    if (subkey_index >= key_value_pb.subkey_size()) {
      return STATUS_SUBSTITUTE(InvalidArgument,
                               "Size of subkeys ($0) must be larger than subkey_index ($1)",
                               key_value_pb.subkey_size(), subkey_index);
    }

    PrimitiveValue subkey_primitive;
    RETURN_NOT_OK(PrimitiveValueFromSubKey(key_value_pb.subkey(subkey_index), &subkey_primitive));
    encoded_subdoc_key = DocKey::EncodedFromRedisKey(key_value_pb.hash_code(), key_value_pb.key());
    subkey_primitive.AppendToKey(&encoded_subdoc_key);
  }
  SubDocument doc;
  bool doc_found = false;
  // Use the cached entry if possible to determine the value type.
  boost::optional<DocWriteBatchCache::Entry> cached_entry;
  if (doc_write_batch) {
    cached_entry = doc_write_batch->LookupCache(encoded_subdoc_key);
  }

  if (cached_entry) {
    doc_found = true;
    doc = SubDocument(cached_entry->value_type);
  } else {
    // TODO(dtxn) - pass correct transaction context when we implement cross-shard transactions
    // support for Redis.
    GetSubDocumentData data = { encoded_subdoc_key, &doc, &doc_found };
    data.return_type_only = true;
    data.exp.always_override = always_override;
    RETURN_NOT_OK(GetSubDocument(iterator, data, /* projection */ nullptr,
        SeekFwdSuffices::kFalse));
  }

  if (!doc_found) {
    return REDIS_TYPE_NONE;
  }

  switch (doc.value_type()) {
    case ValueType::kInvalid: FALLTHROUGH_INTENDED;
    case ValueType::kTombstone:
      return REDIS_TYPE_NONE;
    case ValueType::kObject:
      return REDIS_TYPE_HASH;
    case ValueType::kRedisSet:
      return REDIS_TYPE_SET;
    case ValueType::kRedisTS:
      return REDIS_TYPE_TIMESERIES;
    case ValueType::kRedisSortedSet:
      return REDIS_TYPE_SORTEDSET;
    case ValueType::kRedisList:
      return REDIS_TYPE_LIST;
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
    IntentAwareIterator* iterator,
    const RedisKeyValuePB &key_value_pb,
    int subkey_index = kNilSubkeyIndex,
    bool always_override = false,
    Expiration* exp = nullptr) {
  if (!key_value_pb.has_key()) {
    return STATUS(Corruption, "Expected KeyValuePB");
  }
  auto encoded_doc_key = DocKey::EncodedFromRedisKey(key_value_pb.hash_code(), key_value_pb.key());

  if (!key_value_pb.subkey().empty()) {
    if (key_value_pb.subkey().size() != 1 && subkey_index == kNilSubkeyIndex) {
      return STATUS_SUBSTITUTE(Corruption,
                               "Expected at most one subkey, got $0", key_value_pb.subkey().size());
    }
    PrimitiveValue subkey_primitive;
    RETURN_NOT_OK(PrimitiveValueFromSubKey(
        key_value_pb.subkey(subkey_index == kNilSubkeyIndex ? 0 : subkey_index),
        &subkey_primitive));
    subkey_primitive.AppendToKey(&encoded_doc_key);
  }

  SubDocument doc;
  bool doc_found = false;

  // TODO(dtxn) - pass correct transaction context when we implement cross-shard transactions
  // support for Redis.
  GetSubDocumentData data = { encoded_doc_key, &doc, &doc_found };
  data.exp.always_override = always_override;
  RETURN_NOT_OK(GetSubDocument(iterator, data, /* projection */ nullptr, SeekFwdSuffices::kFalse));
  if (!doc_found) {
    return RedisValue{REDIS_TYPE_NONE};
  }

  bool has_expired = false;
  CHECK_OK(HasExpiredTTL(data.exp.write_ht, data.exp.ttl,
                         iterator->read_time().read, &has_expired));
  if (has_expired)
    return RedisValue{REDIS_TYPE_NONE};

  if (exp)
    *exp = data.exp;

  if (!doc.IsPrimitive()) {
    switch (doc.value_type()) {
      case ValueType::kObject:
        return RedisValue{REDIS_TYPE_HASH};
      case ValueType::kRedisTS:
        return RedisValue{REDIS_TYPE_TIMESERIES};
      case ValueType::kRedisSortedSet:
        return RedisValue{REDIS_TYPE_SORTEDSET};
      case ValueType::kRedisSet:
        return RedisValue{REDIS_TYPE_SET};
      case ValueType::kRedisList:
        return RedisValue{REDIS_TYPE_LIST};
      default:
        return STATUS_SUBSTITUTE(IllegalState, "Invalid value type: $0",
                                 static_cast<int>(doc.value_type()));
    }
  }

  auto val = RedisValue{REDIS_TYPE_STRING, doc.GetString(), data.exp};
  return val;
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
      response->set_code(RedisResponsePB_RedisStatusCode_NIL);
    } else {
      response->set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
    }
    return verify_success_if_missing;
  }
  if (actual_type != expected_type) {
    response->set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    response->set_error_message(wrong_type_message);
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
    response->set_error_message(wrong_type_message);
    return false;
  }
  response->set_code(RedisResponsePB_RedisStatusCode_OK);
  return true;
}

CHECKED_STATUS AddPrimitiveValueToResponseArray(const PrimitiveValue& value,
                                                RedisArrayPB* redis_array) {
  switch (value.value_type()) {
    case ValueType::kString: FALLTHROUGH_INTENDED;
    case ValueType::kStringDescending: {
      redis_array->add_elements(value.GetString());
      return Status::OK();
    }
    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kInt64Descending: {
      redis_array->add_elements(std::to_string(value.GetInt64()));
      return Status::OK();
    }
    case ValueType::kDouble: FALLTHROUGH_INTENDED;
    case ValueType::kDoubleDescending: {
      redis_array->add_elements(std::to_string(value.GetDouble()));
      return Status::OK();
    }
    default:
      return STATUS_SUBSTITUTE(InvalidArgument, "Invalid value type: $0",
                             static_cast<int>(value.value_type()));
  }
}

CHECKED_STATUS CheckUserTimestampForCollections(const UserTimeMicros user_timestamp) {
  if (user_timestamp != Value::kInvalidUserTimestamp) {
    return STATUS(InvalidArgument, "User supplied timestamp is only allowed for "
        "replacing the whole collection");
  }
  return Status::OK();
}

CHECKED_STATUS AddResponseValuesGeneric(const PrimitiveValue& first,
                                        const PrimitiveValue& second,
                                        RedisResponsePB* response,
                                        bool add_keys,
                                        bool add_values,
                                        bool reverse = false) {
  if (add_keys) {
    RETURN_NOT_OK(AddPrimitiveValueToResponseArray(first, response->mutable_array_response()));
  }
  if (add_values) {
    RETURN_NOT_OK(AddPrimitiveValueToResponseArray(second, response->mutable_array_response()));
  }
  return Status::OK();
}

// Populate the response array for sorted sets range queries.
// first refers to the score for the given values.
// second refers to a subdocument where each key is a value with the given score.
CHECKED_STATUS AddResponseValuesSortedSets(const PrimitiveValue& first,
                                           const SubDocument& second,
                                           RedisResponsePB* response,
                                           bool add_keys,
                                           bool add_values,
                                           bool reverse = false) {
  if (reverse) {
    for (auto it = second.object_container().rbegin();
         it != second.object_container().rend();
         it++) {
      const PrimitiveValue& value = it->first;
      RETURN_NOT_OK(AddResponseValuesGeneric(value, first, response, add_values, add_keys));
    }
  } else {
    for (const auto& kv : second.object_container()) {
      const PrimitiveValue& value = kv.first;
      RETURN_NOT_OK(AddResponseValuesGeneric(value, first, response, add_values, add_keys));
    }
  }
  return Status::OK();
}

template <typename T, typename AddResponseRow>
CHECKED_STATUS PopulateRedisResponseFromInternal(T iter,
                                                 AddResponseRow add_response_row,
                                                 const T& iter_end,
                                                 RedisResponsePB *response,
                                                 bool add_keys,
                                                 bool add_values,
                                                 bool reverse = false) {
  response->set_allocated_array_response(new RedisArrayPB());
  for (; iter != iter_end; iter++) {
    RETURN_NOT_OK(add_response_row(
        iter->first, iter->second, response, add_keys, add_values, reverse));
  }
  return Status::OK();
}

template <typename AddResponseRow>
CHECKED_STATUS PopulateResponseFrom(const SubDocument::ObjectContainer &key_values,
                                    AddResponseRow add_response_row,
                                    RedisResponsePB *response,
                                    bool add_keys,
                                    bool add_values,
                                    bool reverse = false) {
  if (reverse) {
    return PopulateRedisResponseFromInternal(key_values.rbegin(), add_response_row,
                                             key_values.rend(), response, add_keys,
                                             add_values, reverse);
  } else {
    return PopulateRedisResponseFromInternal(key_values.begin(),  add_response_row,
                                             key_values.end(), response, add_keys,
                                             add_values, reverse);
  }
}

void SetOptionalInt(RedisDataType type, int64_t value, int64_t none_value,
                    RedisResponsePB* response) {
  if (type == RedisDataType::REDIS_TYPE_NONE) {
    response->set_code(RedisResponsePB_RedisStatusCode_NIL);
    response->set_int_response(none_value);
  } else {
    response->set_code(RedisResponsePB_RedisStatusCode_OK);
    response->set_int_response(value);
  }
}

void SetOptionalInt(RedisDataType type, int64_t value, RedisResponsePB* response) {
  SetOptionalInt(type, value, 0, response);
}

Result<int64_t> GetCardinality(IntentAwareIterator* iterator,
                              const RedisKeyValuePB& kv) {
  auto encoded_key_card = DocKey::EncodedFromRedisKey(kv.hash_code(), kv.key());
  PrimitiveValue(ValueType::kCounter).AppendToKey(&encoded_key_card);
  SubDocument subdoc_card;

  bool subdoc_card_found = false;
  GetSubDocumentData data = { encoded_key_card, &subdoc_card, &subdoc_card_found };

  RETURN_NOT_OK(GetSubDocument(iterator, data, /* projection */ nullptr, SeekFwdSuffices::kFalse));

  return subdoc_card_found ? subdoc_card.GetInt64() : 0;
}

template <typename AddResponseValues>
CHECKED_STATUS GetAndPopulateResponseValues(
    IntentAwareIterator* iterator,
    AddResponseValues add_response_values,
    const GetSubDocumentData& data,
    ValueType expected_type,
    const RedisReadRequestPB& request,
    RedisResponsePB* response,
    bool add_keys, bool add_values, bool reverse) {

  RETURN_NOT_OK(GetSubDocument(iterator, data, /* projection */ nullptr, SeekFwdSuffices::kFalse));

  // Validate and populate response.
  response->set_allocated_array_response(new RedisArrayPB());
  if (!(*data.doc_found)) {
      response->set_code(RedisResponsePB_RedisStatusCode_NIL);
      return Status::OK();
  }

  if (VerifyTypeAndSetCode(expected_type, data.result->value_type(), response)) {
      RETURN_NOT_OK(PopulateResponseFrom(data.result->object_container(),
                                         add_response_values,
                                         response, add_keys, add_values, reverse));
  }
  return Status::OK();
}

// Get normalized (with respect to card) upper and lower index bounds for reverse range scans.
void GetNormalizedBounds(int64 low_idx, int64 high_idx, int64 card, bool reverse,
                         int64* low_idx_normalized, int64* high_idx_normalized) {
  // Turn negative bounds positive.
  if (low_idx < 0) {
    low_idx = card + low_idx;
  }
  if (high_idx < 0) {
    high_idx = card + high_idx;
  }

  // Index from lower to upper instead of upper to lower.
  if (reverse) {
    *low_idx_normalized = card - high_idx - 1;
    *high_idx_normalized = card - low_idx - 1;
  } else {
    *low_idx_normalized = low_idx;
    *high_idx_normalized = high_idx;
  }

  // Fit bounds to range [0, card).
  if (*low_idx_normalized < 0) {
    *low_idx_normalized = 0;
  }
  if (*high_idx_normalized >= card) {
    *high_idx_normalized = card - 1;
  }
}

CHECKED_STATUS FindMemberForIndex(const QLColumnValuePB& column_value,
                                  size_t index,
                                  rapidjson::Value* document,
                                  rapidjson::Value::MemberIterator* memberit,
                                  rapidjson::Value::ValueIterator* valueit,
                                  bool* last_elem_object) {
  int64_t array_index;
  if (document->IsArray()) {
    util::VarInt varint;
    RETURN_NOT_OK(varint.DecodeFromComparable(
        column_value.json_args(index).operand().value().varint_value()));
    RETURN_NOT_OK(varint.ToInt64(&array_index));

    if (array_index >= document->GetArray().Size() || array_index < 0) {
      return STATUS_SUBSTITUTE(QLError, "Array index out of bounds: ", array_index);
    }
    *valueit = document->Begin();
    std::advance(*valueit, array_index);
    *last_elem_object = false;
  } else {
    const auto& member = column_value.json_args(index).operand().value().string_value().c_str();
    *memberit = document->FindMember(member);
    if (*memberit == document->MemberEnd()) {
      return STATUS_SUBSTITUTE(QLError, "Could not find member: ", member);
    }
    *last_elem_object = true;
  }
  return Status::OK();
}

} // anonymous namespace

void RedisWriteOperation::InitializeIterator(const DocOperationApplyData& data) {
  auto subdoc_key = SubDocKey(DocKey::FromRedisKey(
      request_.key_value().hash_code(), request_.key_value().key()));

  auto iter = yb::docdb::CreateIntentAwareIterator(
      data.doc_write_batch->doc_db(),
      BloomFilterMode::USE_BLOOM_FILTER, subdoc_key.Encode().AsSlice(),
      redis_query_id(), /* txn_op_context */ boost::none, data.deadline, data.read_time);

  iterator_ = std::move(iter);
}

Status RedisWriteOperation::Apply(const DocOperationApplyData& data) {
  switch (request_.request_case()) {
    case RedisWriteRequestPB::RequestCase::kSetRequest:
      return ApplySet(data);
    case RedisWriteRequestPB::RequestCase::kSetTtlRequest:
      return ApplySetTtl(data);
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
    // TODO: Cut this short in doc_operation.
    case RedisWriteRequestPB::RequestCase::kNoOpRequest:
      return Status::OK();
    case RedisWriteRequestPB::RequestCase::REQUEST_NOT_SET: break;
  }
  return STATUS(Corruption,
      Substitute("Unsupported redis read operation: $0", request_.request_case()));
}

Result<RedisDataType> RedisWriteOperation::GetValueType(
    const DocOperationApplyData& data, int subkey_index) {
  if (!iterator_) {
    InitializeIterator(data);
  }
  return GetRedisValueType(
      iterator_.get(), request_.key_value(), data.doc_write_batch, subkey_index);
}

Result<RedisValue> RedisWriteOperation::GetValue(
    const DocOperationApplyData& data, int subkey_index, Expiration* ttl) {
  if (!iterator_) {
    InitializeIterator(data);
  }
  return GetRedisValue(iterator_.get(), request_.key_value(),
                       subkey_index, /* always_override */ false, ttl);
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
          response_.set_error_message(wrong_type_message);
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
        if (kv.subkey_size() == 1 && EmulateRedisResponse(kv.type()) &&
            !request_.set_request().expect_ok_response()) {
          auto type = GetValueType(data, 0);
          RETURN_NOT_OK(type);
          // For HSET/TSADD, we return 0 or 1 depending on if the key already existed.
          // If flag is false, no int response is returned.
          SetOptionalInt(*type, 0, 1, &response_);
        }
        if (*data_type == REDIS_TYPE_NONE && kv.type() == REDIS_TYPE_TIMESERIES) {
          // Need to insert the document instead of extending it.
          RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
              doc_path, kv_entries, data.read_time, data.deadline, redis_query_id(), ttl,
              Value::kInvalidUserTimestamp, false /* init_marker_ttl */));
        } else {
          RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
              doc_path, kv_entries, data.read_time, data.deadline, redis_query_id(), ttl));
        }
        break;
      }
      case REDIS_TYPE_SORTEDSET: {
        if (*data_type != kv.type() && *data_type != REDIS_TYPE_NONE) {
          response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
          response_.set_error_message(wrong_type_message);
          return Status::OK();
        }

        // The SubDocuments to be inserted for card, the forward mapping, and reverse mapping.
        SubDocument kv_entries_card;
        SubDocument kv_entries_forward;
        SubDocument kv_entries_reverse;

        // The top level mapping.
        SubDocument kv_entries;

        int new_elements_added = 0;
        int return_value = 0;
        for (int i = 0; i < kv.subkey_size(); i++) {
          // Check whether the value is already in the document, if so delete it.
          SubDocKey key_reverse = SubDocKey(DocKey::FromRedisKey(kv.hash_code(), kv.key()),
                                            PrimitiveValue(ValueType::kSSReverse),
                                            PrimitiveValue(kv.value(i)));
          SubDocument subdoc_reverse;
          bool subdoc_reverse_found = false;
          auto encoded_key_reverse = key_reverse.EncodeWithoutHt();
          GetSubDocumentData get_data = { encoded_key_reverse, &subdoc_reverse,
                                          &subdoc_reverse_found };
          RETURN_NOT_OK(GetSubDocument(
              data.doc_write_batch->doc_db(),
              get_data, redis_query_id(), boost::none /* txn_op_context */, data.deadline,
              data.read_time));

          // Flag indicating whether we should add the given entry to the sorted set.
          bool should_add_entry = true;
          // Flag indicating whether we shoould remove an entry from the sorted set.
          bool should_remove_existing_entry = false;

          if (!subdoc_reverse_found) {
            // The value is not already in the document.
            switch (request_.set_request().sorted_set_options().update_options()) {
              case SortedSetOptionsPB_UpdateOptions_NX: FALLTHROUGH_INTENDED;
              case SortedSetOptionsPB_UpdateOptions_NONE: {
                // Both these options call for inserting new elements, increment return_value and
                // keep should_add_entry as true.
                return_value++;
                new_elements_added++;
                break;
              }
              default: {
                // XX option calls for no new elements, don't increment return_value and set
                // should_add_entry to false.
                should_add_entry = false;
                break;
              }
            }
          } else {
            // The value is already in the document.
            switch (request_.set_request().sorted_set_options().update_options()) {
              case SortedSetOptionsPB_UpdateOptions_XX:
              case SortedSetOptionsPB_UpdateOptions_NONE: {
                // First make sure that the new score is different from the old score.
                // Both these options call for updating existing elements, set
                // should_remove_existing_entry to true, and if the CH flag is on (return both
                // elements changed and elements added), increment return_value.
                double score_to_remove = subdoc_reverse.GetDouble();
                if (score_to_remove != kv.subkey(i).double_subkey()) {
                  should_remove_existing_entry = true;
                  if (request_.set_request().sorted_set_options().ch()) {
                    return_value++;
                  }
                }
                break;
              }
              default: {
                // NX option calls for only new elements, set should_add_entry to false.
                should_add_entry = false;
                break;
              }
            }
          }

          if (should_remove_existing_entry) {
            double score_to_remove = subdoc_reverse.GetDouble();
            SubDocument subdoc_forward_tombstone;
            subdoc_forward_tombstone.SetChild(PrimitiveValue(kv.value(i)),
                                              SubDocument(ValueType::kTombstone));
            kv_entries_forward.SetChild(PrimitiveValue::Double(score_to_remove),
                                        SubDocument(subdoc_forward_tombstone));
          }

          if (should_add_entry) {
            // If the incr option is specified, we need insert the existing score + new score
            // instead of just the new score.
            double score_to_add = request_.set_request().sorted_set_options().incr() ?
                kv.subkey(i).double_subkey() + subdoc_reverse.GetDouble() :
                kv.subkey(i).double_subkey();

            // Add the forward mapping to the entries.
            SubDocument *forward_entry =
                kv_entries_forward.GetOrAddChild(PrimitiveValue::Double(score_to_add)).first;
            forward_entry->SetChild(PrimitiveValue(kv.value(i)),
                                    SubDocument(PrimitiveValue()));

            // Add the reverse mapping to the entries.
            kv_entries_reverse.SetChild(PrimitiveValue(kv.value(i)),
                                        SubDocument(PrimitiveValue::Double(score_to_add)));
          }
        }

        if (new_elements_added > 0) {
          int64_t card = VERIFY_RESULT(GetCardinality(iterator_.get(), kv));
          // Insert card + new_elements_added back into the document for the updated card.
          kv_entries_card = SubDocument(PrimitiveValue(card + new_elements_added));
          kv_entries.SetChild(PrimitiveValue(ValueType::kCounter), SubDocument(kv_entries_card));
        }

        if (kv_entries_forward.object_num_keys() > 0) {
          kv_entries.SetChild(PrimitiveValue(ValueType::kSSForward),
                              SubDocument(kv_entries_forward));
        }

        if (kv_entries_reverse.object_num_keys() > 0) {
          kv_entries.SetChild(PrimitiveValue(ValueType::kSSReverse),
                              SubDocument(kv_entries_reverse));
        }

        if (kv_entries.object_num_keys() > 0) {
          RETURN_NOT_OK(kv_entries.ConvertToRedisSortedSet());
          if (*data_type == REDIS_TYPE_NONE) {
                RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
                    doc_path, kv_entries, data.read_time, data.deadline, redis_query_id(), ttl));
          } else {
                RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
                    doc_path, kv_entries, data.read_time, data.deadline, redis_query_id(), ttl));
          }
        }
        response_.set_code(RedisResponsePB_RedisStatusCode_OK);
        response_.set_int_response(return_value);
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
        response_.set_code(RedisResponsePB_RedisStatusCode_NIL);
        return Status::OK();
      }
    }
    RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
        doc_path, Value(PrimitiveValue(kv.value(0)), ttl),
        data.read_time, data.deadline, redis_query_id()));
  }
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

Status RedisWriteOperation::ApplySetTtl(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();

  // We only support setting TTLs on top-level keys.
  if (!kv.subkey().empty()) {
    return STATUS_SUBSTITUTE(Corruption,
                             "Expected no subkeys, got $0", kv.subkey().size());
  }

  MonoDelta ttl;
  bool absolute_expiration = request_.set_ttl_request().has_absolute_time();

  // Handle ExpireAt
  if (absolute_expiration) {
    int64_t calc_ttl = request_.set_ttl_request().absolute_time() -
      server::HybridClock::GetPhysicalValueNanos(data.read_time.read) /
      MonoTime::kNanosecondsPerMillisecond;
    if (calc_ttl <= 0) {
      return ApplyDel(data);
    }
    ttl = MonoDelta::FromMilliseconds(calc_ttl);
  }

  Expiration exp;
  auto value = GetValue(data, kNilSubkeyIndex, &exp);
  RETURN_NOT_OK(value);

  if (value->type == REDIS_TYPE_TIMESERIES) { // This command is not supported.
    return STATUS_SUBSTITUTE(InvalidCommand,
        "Redis data type $0 not supported in EXPIRE and PERSIST commands", value->type);
  }

  if (value->type == REDIS_TYPE_NONE) { // Key does not exist.
    response_.set_int_response(0);
    return Status::OK();
  }

  if (!absolute_expiration && request_.set_ttl_request().ttl() == -1) { // Handle PERSIST.
    MonoDelta new_ttl = VERIFY_RESULT(exp.ComputeRelativeTtl(iterator_->read_time().read));
    if (new_ttl.IsNegative() || new_ttl == Value::kMaxTtl) {
      response_.set_int_response(0);
      return Status::OK();
    }
  }

  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
  if (!absolute_expiration) {
    ttl = request_.set_ttl_request().ttl() == -1 ? Value::kMaxTtl :
      MonoDelta::FromMilliseconds(request_.set_ttl_request().ttl());
  }

  ValueType v_type = ValueTypeFromRedisType(value->type);
  if (v_type == ValueType::kInvalid)
    return STATUS(Corruption, "Invalid value type.");

  RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
      doc_path, Value(PrimitiveValue(v_type), ttl, Value::kInvalidUserTimestamp, Value::kTtlFlag),
      data.read_time, data.deadline, redis_query_id()));
  response_.set_int_response(1);
  return Status::OK();
}

Status RedisWriteOperation::ApplyGetSet(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();

  if (kv.value_size() != 1) {
    return STATUS_SUBSTITUTE(Corruption,
        "Getset kv should have 1 value, found $0", kv.value_size());
  }

  auto value = GetValue(data);
  RETURN_NOT_OK(value);

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_,
      VerifySuccessIfMissing::kTrue)) {
    // We've already set the error code in the response.
    return Status::OK();
  }
  response_.set_string_response(value->value);

  return data.doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(kv.value(0))), data.read_time, data.deadline, redis_query_id());
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

  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  response_.set_int_response(value->value.length());

  // TODO: update the TTL with the write time rather than read time,
  // or store the expiration.
  return data.doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(value->value),
            VERIFY_RESULT(value->exp.ComputeRelativeTtl(iterator_->read_time().read))),
      data.read_time,
      data.deadline,
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
    response_.set_error_message(wrong_type_message);
    return Status::OK();
  }

  SubDocument values;
  // Number of distinct keys being removed.
  int num_keys = 0;
  switch (kv.type()) {
    case REDIS_TYPE_NONE: {
      values = SubDocument(ValueType::kTombstone);
      num_keys = *data_type == REDIS_TYPE_NONE ? 0 : 1;
      break;
    }
    case REDIS_TYPE_TIMESERIES: {
      if (*data_type == REDIS_TYPE_NONE) {
        return Status::OK();
      }
      for (int i = 0; i < kv.subkey_size(); i++) {
        PrimitiveValue primitive_value;
        RETURN_NOT_OK(PrimitiveValueFromSubKeyStrict(kv.subkey(i), *data_type, &primitive_value));
        values.SetChild(primitive_value, SubDocument(ValueType::kTombstone));
      }
      num_keys = kv.subkey_size();
      break;
    }
    case REDIS_TYPE_SORTEDSET: {
      SubDocument values_card;
      SubDocument values_forward;
      SubDocument values_reverse;
      num_keys = kv.subkey_size();
      for (int i = 0; i < kv.subkey_size(); i++) {
        // Check whether the value is already in the document.
        SubDocument doc_reverse;
        bool doc_reverse_found = false;
        SubDocKey subdoc_key_reverse = SubDocKey(DocKey::FromRedisKey(kv.hash_code(), kv.key()),
                                                 PrimitiveValue(ValueType::kSSReverse),
                                                 PrimitiveValue(kv.subkey(i).string_subkey()));
        // Todo(Rahul): Add values to the write batch cache and then do an additional check.
        // As of now, we only check to see if a value is in rocksdb, and we should also check
        // the write batch.
        auto encoded_subdoc_key_reverse = subdoc_key_reverse.EncodeWithoutHt();
        GetSubDocumentData get_data = { encoded_subdoc_key_reverse, &doc_reverse,
                                        &doc_reverse_found };
        RETURN_NOT_OK(GetSubDocument(
            data.doc_write_batch->doc_db(),
            get_data, redis_query_id(), boost::none /* txn_op_context */, data.deadline,
            data.read_time));
        if (doc_reverse_found && doc_reverse.value_type() != ValueType::kTombstone) {
          // The value is already in the doc, needs to be removed.
          values_reverse.SetChild(PrimitiveValue(kv.subkey(i).string_subkey()),
                          SubDocument(ValueType::kTombstone));
          // For sorted sets, the forward mapping also needs to be deleted.
          SubDocument doc_forward;
          doc_forward.SetChild(PrimitiveValue(kv.subkey(i).string_subkey()),
                               SubDocument(ValueType::kTombstone));
          values_forward.SetChild(PrimitiveValue::Double(doc_reverse.GetDouble()),
                          SubDocument(doc_forward));
        } else {
          // If the key is absent, it doesn't contribute to the count of keys being deleted.
          num_keys--;
        }
      }
      int64_t card = VERIFY_RESULT(GetCardinality(iterator_.get(), kv));
      // The new cardinality is card - num_keys.
      values_card = SubDocument(PrimitiveValue(card - num_keys));

      values.SetChild(PrimitiveValue(ValueType::kCounter), SubDocument(values_card));
      values.SetChild(PrimitiveValue(ValueType::kSSForward), SubDocument(values_forward));
      values.SetChild(PrimitiveValue(ValueType::kSSReverse), SubDocument(values_reverse));

      break;
    }
    default: {
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
      }
      break;
    }
  }

  if (num_keys != 0) {
    DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
    RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(doc_path, values,
        data.read_time, data.deadline, redis_query_id()));
  }

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

  if (request_.set_range_request().offset() > value->value.length()) {
    value->value.resize(request_.set_range_request().offset(), 0);
  }
  value->value.replace(request_.set_range_request().offset(), kv.value(0).length(), kv.value(0));
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  response_.set_int_response(value->value.length());

  // TODO: update the TTL with the write time rather than read time,
  // or store the expiration.
  Value new_val = Value(PrimitiveValue(value->value),
        VERIFY_RESULT(value->exp.ComputeRelativeTtl(iterator_->read_time().read)));
  return data.doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()), new_val, std::move(iterator_));
}

Status RedisWriteOperation::ApplyIncr(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();
  const int64_t incr = request_.incr_request().increment_int();

  if (kv.type() != REDIS_TYPE_HASH && kv.type() != REDIS_TYPE_STRING) {
    return STATUS_SUBSTITUTE(InvalidCommand,
                             "Redis data type $0 not supported in Incr command", kv.type());
  }

  auto container_type = GetValueType(data);
  RETURN_NOT_OK(container_type);
  if (!VerifyTypeAndSetCode(kv.type(), *container_type, &response_,
                            VerifySuccessIfMissing::kTrue)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  int subkey = (kv.type() == REDIS_TYPE_HASH ? 0 : -1);
  auto value = GetValue(data, subkey);
  RETURN_NOT_OK(value);

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_,
      VerifySuccessIfMissing::kTrue)) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    response_.set_error_message(wrong_type_message);
    return Status::OK();
  }

  // If no value is present, 0 is the default.
  int64_t old_value = 0, new_value;
  if (value->type != REDIS_TYPE_NONE) {
    auto old = util::CheckedStoll(value->value);
    if (!old.ok()) {
      // This can happen if there are leading or trailing spaces, or the value
      // is out of range.
      response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
      response_.set_error_message("ERR value is not an integer or out of range");
      return Status::OK();
    }
    old_value = *old;
  }

  if ((incr < 0 && old_value < 0 && incr < numeric_limits<int64_t>::min() - old_value) ||
      (incr > 0 && old_value > 0 && incr > numeric_limits<int64_t>::max() - old_value)) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    response_.set_error_message("Increment would overflow");
    return Status::OK();
  }
  new_value = old_value + incr;
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  response_.set_int_response(new_value);

  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
  PrimitiveValue new_pvalue = PrimitiveValue(std::to_string(new_value));
  if (kv.type() == REDIS_TYPE_HASH) {
    SubDocument kv_entries = SubDocument();
    PrimitiveValue subkey_value;
    RETURN_NOT_OK(PrimitiveValueFromSubKeyStrict(kv.subkey(0), kv.type(), &subkey_value));
    kv_entries.SetChild(subkey_value, SubDocument(new_pvalue));
    return data.doc_write_batch->ExtendSubDocument(
        doc_path, kv_entries, data.read_time, data.deadline, redis_query_id());
  } else {  // kv.type() == REDIS_TYPE_STRING
    // TODO: update the TTL with the write time rather than read time,
    // or store the expiration.
    Value new_val = Value(new_pvalue,
        VERIFY_RESULT(value->exp.ComputeRelativeTtl(iterator_->read_time().read)));
    return data.doc_write_batch->SetPrimitive(doc_path, new_val, std::move(iterator_));
  }
}

Status RedisWriteOperation::ApplyPush(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();
  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
  RedisDataType data_type = VERIFY_RESULT(GetValueType(data));
  if (data_type != REDIS_TYPE_LIST && data_type != REDIS_TYPE_NONE) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    response_.set_error_message(wrong_type_message);
    return Status::OK();
  }

  SubDocument list;
  int64_t card = VERIFY_RESULT(GetCardinality(iterator_.get(), kv)) + kv.value_size();
  list.SetChild(PrimitiveValue(ValueType::kCounter), SubDocument(PrimitiveValue(card)));

  SubDocument elements(request_.push_request().side() == REDIS_SIDE_LEFT ?
                   ListExtendOrder::PREPEND : ListExtendOrder::APPEND);
  for (auto val : kv.value()) {
    elements.AddListElement(SubDocument(PrimitiveValue(val)));
  }
  list.SetChild(PrimitiveValue(ValueType::kArray), std::move(elements));
  RETURN_NOT_OK(list.ConvertToRedisList());

  if (data_type == REDIS_TYPE_NONE) {
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()), list,
        data.read_time, data.deadline, redis_query_id()));
  } else {
    RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
        DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()), list,
        data.read_time, data.deadline, redis_query_id()));
  }

  response_.set_int_response(card);
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

Status RedisWriteOperation::ApplyInsert(const DocOperationApplyData& data) {
  return STATUS(NotSupported, "Redis operation has not been implemented");
}

Status RedisWriteOperation::ApplyPop(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();
  DocPath doc_path = DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key());
  RedisDataType data_type = VERIFY_RESULT(GetValueType(data));

  if (!VerifyTypeAndSetCode(kv.type(), data_type, &response_, VerifySuccessIfMissing::kTrue)) {
    // We already set the error code in the function.
    return Status::OK();
  }

  SubDocument list;
  int64_t card = VERIFY_RESULT(GetCardinality(iterator_.get(), kv));

  if (!card) {
    response_.set_code(RedisResponsePB_RedisStatusCode_NIL);
    return Status::OK();
  }

  std::vector<int> indices;
  std::vector<SubDocument> new_value = {SubDocument(PrimitiveValue(ValueType::kTombstone))};
  std::vector<std::string> value;

  if (request_.pop_request().side() == REDIS_SIDE_LEFT) {
    indices.push_back(1);
    RETURN_NOT_OK(data.doc_write_batch->ReplaceInList(doc_path, indices, new_value,
        data.read_time, data.deadline, redis_query_id(), Direction::kForward, 0, &value));
  } else {
    indices.push_back(card);
    RETURN_NOT_OK(data.doc_write_batch->ReplaceInList(doc_path, indices, new_value,
        data.read_time, data.deadline, redis_query_id(), Direction::kBackward, card + 1, &value));
  }

  list.SetChild(PrimitiveValue(ValueType::kCounter), SubDocument(PrimitiveValue(--card)));
  RETURN_NOT_OK(list.ConvertToRedisList());
  RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
        doc_path, list, data.read_time, data.deadline, redis_query_id()));

  if (value.size() != 1)
    return STATUS_SUBSTITUTE(Corruption,
                             "Expected one popped value, got $0", value.size());

  response_.set_string_response(value[0]);
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

Status RedisWriteOperation::ApplyAdd(const DocOperationApplyData& data) {
  const RedisKeyValuePB& kv = request_.key_value();
  auto data_type = GetValueType(data);
  RETURN_NOT_OK(data_type);

  if (*data_type != REDIS_TYPE_SET && *data_type != REDIS_TYPE_NONE) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    response_.set_error_message(wrong_type_message);
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
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        doc_path, set_entries, data.read_time, data.deadline, redis_query_id()));
  } else {
    RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
        doc_path, set_entries, data.read_time, data.deadline, redis_query_id()));
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
  SubDocKey doc_key(
      DocKey::FromRedisKey(request_.key_value().hash_code(), request_.key_value().key()));
  auto iter = yb::docdb::CreateIntentAwareIterator(
      doc_db_, BloomFilterMode::USE_BLOOM_FILTER,
      doc_key.Encode().AsSlice(),
      redis_query_id(), /* txn_op_context */ boost::none, deadline_, read_time_);
  iterator_ = std::move(iter);

  switch (request_.request_case()) {
    case RedisReadRequestPB::RequestCase::kGetRequest:
      return ExecuteGet();
    case RedisReadRequestPB::RequestCase::kGetTtlRequest:
      return ExecuteGetTtl();
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
          Substitute("Unsupported redis read operation: $0", request_.request_case()));
  }
}

int RedisReadOperation::ApplyIndex(int32_t index, const int32_t len) {
  if (index < 0) index += len;
  if (index < 0) index = 0;
  if (index > len) index = len;
  return index;
}

Status RedisReadOperation::ExecuteHGetAllLikeCommands(ValueType value_type,
                                                      bool add_keys,
                                                      bool add_values) {
  SubDocKey doc_key(
      DocKey::FromRedisKey(request_.key_value().hash_code(), request_.key_value().key()));
  SubDocument doc;
  bool doc_found = false;
  auto encoded_doc_key = doc_key.EncodeWithoutHt();

  // TODO(dtxn) - pass correct transaction context when we implement cross-shard transactions
  // support for Redis.
  GetSubDocumentData data = { encoded_doc_key, &doc, &doc_found };

  bool has_cardinality_subkey = value_type == ValueType::kRedisSortedSet ||
                                value_type == ValueType::kRedisList;
  bool return_array_response = add_keys || add_values;

  if (has_cardinality_subkey) {
    data.return_type_only = !return_array_response;
  } else {
    data.count_only = !return_array_response;
  }

  RETURN_NOT_OK(GetSubDocument(iterator_.get(), data, /* projection */ nullptr,
                               SeekFwdSuffices::kFalse));
  if (return_array_response)
    response_.set_allocated_array_response(new RedisArrayPB());

  if (!doc_found) {
    response_.set_code(RedisResponsePB_RedisStatusCode_OK);
    if (!return_array_response)
      response_.set_int_response(0);
    return Status::OK();
  }

  if (VerifyTypeAndSetCode(value_type, doc.value_type(), &response_)) {
    if (return_array_response) {
      RETURN_NOT_OK(PopulateResponseFrom(doc.object_container(), AddResponseValuesGeneric,
                                         &response_, add_keys, add_values));
    } else {
      int64_t card = has_cardinality_subkey ?
        VERIFY_RESULT(GetCardinality(iterator_.get(), request_.key_value())) :
        data.record_count;
      response_.set_int_response(card);
      response_.set_code(RedisResponsePB_RedisStatusCode_OK);
    }
  }
  return Status::OK();
}

Status RedisReadOperation::ExecuteCollectionGetRange() {
  const RedisKeyValuePB& key_value = request_.key_value();
  if (!request_.has_key_value() || !key_value.has_key()) {
    return STATUS(InvalidArgument, "Need to specify the key");
  }

  const auto request_type = request_.get_collection_range_request().request_type();
  switch (request_type) {
    case RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSREVRANGEBYTIME:
      FALLTHROUGH_INTENDED;
    case RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZRANGEBYSCORE: FALLTHROUGH_INTENDED;
    case RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSRANGEBYTIME: {
      if(!request_.has_subkey_range() || !request_.subkey_range().has_lower_bound() ||
          !request_.subkey_range().has_upper_bound()) {
        return STATUS(InvalidArgument, "Need to specify the subkey range");
      }
      const RedisSubKeyBoundPB& lower_bound = request_.subkey_range().lower_bound();
      const RedisSubKeyBoundPB& upper_bound = request_.subkey_range().upper_bound();

      if ((lower_bound.has_infinity_type() &&
          lower_bound.infinity_type() == RedisSubKeyBoundPB_InfinityType_POSITIVE) ||
          (upper_bound.has_infinity_type() &&
              upper_bound.infinity_type() == RedisSubKeyBoundPB_InfinityType_NEGATIVE)) {
        // Return empty response.
        response_.set_code(RedisResponsePB_RedisStatusCode_OK);
        RETURN_NOT_OK(PopulateResponseFrom(SubDocument::ObjectContainer(), AddResponseValuesGeneric,
                                           &response_, /* add_keys */ true, /* add_values */ true));
        return Status::OK();
      }

      if (request_type == RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZRANGEBYSCORE) {
        auto type = VERIFY_RESULT(GetValueType());
        auto expected_type = REDIS_TYPE_SORTEDSET;
        if (!VerifyTypeAndSetCode(expected_type, type, &response_, VerifySuccessIfMissing::kTrue)) {
          return Status::OK();
        }
        auto encoded_doc_key = DocKey::EncodedFromRedisKey(
            request_.key_value().hash_code(), request_.key_value().key());
        PrimitiveValue(ValueType::kSSForward).AppendToKey(&encoded_doc_key);
        double low_double = lower_bound.subkey_bound().double_subkey();
        double high_double = upper_bound.subkey_bound().double_subkey();

        KeyBytes low_sub_key_bound;
        KeyBytes high_sub_key_bound;

        SliceKeyBound low_subkey;
        if (!lower_bound.has_infinity_type()) {
          low_sub_key_bound = encoded_doc_key;
          PrimitiveValue::Double(low_double).AppendToKey(&low_sub_key_bound);
          low_subkey = SliceKeyBound(low_sub_key_bound, LowerBound(lower_bound.is_exclusive()));
        }
        SliceKeyBound high_subkey;
        if (!upper_bound.has_infinity_type()) {
          high_sub_key_bound = encoded_doc_key;
          PrimitiveValue::Double(high_double).AppendToKey(&high_sub_key_bound);
          high_subkey = SliceKeyBound(high_sub_key_bound, UpperBound(upper_bound.is_exclusive()));
        }

        bool add_keys = request_.get_collection_range_request().with_scores();

        SubDocument doc;
        bool doc_found = false;
        GetSubDocumentData data = { encoded_doc_key, &doc, &doc_found };
        data.low_subkey = &low_subkey;
        data.high_subkey = &high_subkey;

        IndexBound low_index;
        IndexBound high_index;
        if(request_.has_range_request_limit()) {
          int32_t offset = request_.index_range().lower_bound().index();
          int32_t limit = request_.range_request_limit();

          if (offset < 0 || limit == 0) {
            // Return an empty response.
            response_.set_code(RedisResponsePB_RedisStatusCode_OK);
            RETURN_NOT_OK(PopulateResponseFrom(SubDocument::ObjectContainer(),
                                               AddResponseValuesGeneric,
                                               &response_, /* add_keys */
                                               true, /* add_values */
                                               true));
            return Status::OK();
          }

          low_index = IndexBound(offset, true /* is_lower */);
          data.low_index = &low_index;
          if (limit > 0) {
            // Only define upper bound if limit is positive.
            high_index = IndexBound(offset + limit - 1, false /* is_lower */);
            data.high_index = &high_index;
          }
        }
        RETURN_NOT_OK(GetAndPopulateResponseValues(iterator_.get(), AddResponseValuesSortedSets,
            data, ValueType::kObject, request_, &response_,
            /* add_keys */ add_keys, /* add_values */ true, /* reverse */ false));
      } else {
        auto encoded_doc_key = DocKey::EncodedFromRedisKey(
            request_.key_value().hash_code(), request_.key_value().key());
        int64_t low_timestamp = lower_bound.subkey_bound().timestamp_subkey();
        int64_t high_timestamp = upper_bound.subkey_bound().timestamp_subkey();

        KeyBytes low_sub_key_bound;
        KeyBytes high_sub_key_bound;

        SliceKeyBound low_subkey;
        // Need to switch the order since we store the timestamps in descending order.
        if (!upper_bound.has_infinity_type()) {
          low_sub_key_bound = encoded_doc_key;
          PrimitiveValue(high_timestamp, SortOrder::kDescending).AppendToKey(&low_sub_key_bound);
          low_subkey = SliceKeyBound(low_sub_key_bound, LowerBound(upper_bound.is_exclusive()));
        }
        SliceKeyBound high_subkey;
        if (!lower_bound.has_infinity_type()) {
          high_sub_key_bound = encoded_doc_key;
          PrimitiveValue(low_timestamp, SortOrder::kDescending).AppendToKey(&high_sub_key_bound);
          high_subkey = SliceKeyBound(high_sub_key_bound, UpperBound(lower_bound.is_exclusive()));
        }

        SubDocument doc;
        bool doc_found = false;
        GetSubDocumentData data = { encoded_doc_key, &doc, &doc_found };
        data.low_subkey = &low_subkey;
        data.high_subkey = &high_subkey;
        data.limit = request_.range_request_limit();
        bool is_reverse = true;
        if (request_type == RedisCollectionGetRangeRequestPB_GetRangeRequestType_TSREVRANGEBYTIME) {
          // If reverse is false, newest element is the first element returned.
          is_reverse = false;
        }
        RETURN_NOT_OK(GetAndPopulateResponseValues(iterator_.get(), AddResponseValuesGeneric, data,
            ValueType::kRedisTS, request_, &response_,
            /* add_keys */ true, /* add_values */ true, is_reverse));
      }
      break;
    }
    case RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZRANGE: FALLTHROUGH_INTENDED;
    case RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZREVRANGE: {
      if(!request_.has_index_range() || !request_.index_range().has_lower_bound() ||
          !request_.index_range().has_upper_bound()) {
        return STATUS(InvalidArgument, "Need to specify the index range");
      }

      // First make sure is of type sorted set or none.
      auto type = GetValueType();
      RETURN_NOT_OK(type);
      auto expected_type = RedisDataType::REDIS_TYPE_SORTEDSET;
      if (!VerifyTypeAndSetCode(expected_type, *type, &response_, VerifySuccessIfMissing::kTrue)) {
        return Status::OK();
      }

      int64_t card = VERIFY_RESULT(GetCardinality(iterator_.get(), request_.key_value()));

      const RedisIndexBoundPB& low_index_bound = request_.index_range().lower_bound();
      const RedisIndexBoundPB& high_index_bound = request_.index_range().upper_bound();

      int64 low_idx_normalized, high_idx_normalized;

      int64 low_idx = low_index_bound.index();
      int64 high_idx = high_index_bound.index();
      // Normalize the bounds to be positive and go from low to high index.
      bool reverse = false;
      if (request_type == RedisCollectionGetRangeRequestPB_GetRangeRequestType_ZREVRANGE) {
        reverse = true;
      }
      GetNormalizedBounds(
          low_idx, high_idx, card, reverse, &low_idx_normalized, &high_idx_normalized);

      if (high_idx_normalized < low_idx_normalized) {
        // Return empty response.
        response_.set_code(RedisResponsePB_RedisStatusCode_OK);
        RETURN_NOT_OK(PopulateResponseFrom(SubDocument::ObjectContainer(),
                                           AddResponseValuesGeneric,
                                           &response_, /* add_keys */
                                           true, /* add_values */
                                           true));
        return Status::OK();
      }
      auto encoded_doc_key = DocKey::EncodedFromRedisKey(
          request_.key_value().hash_code(), request_.key_value().key());
      PrimitiveValue(ValueType::kSSForward).AppendToKey(&encoded_doc_key);

      bool add_keys = request_.get_collection_range_request().with_scores();

      IndexBound low_bound = IndexBound(low_idx_normalized, true /* is_lower */);
      IndexBound high_bound = IndexBound(high_idx_normalized, false /* is_lower */);

      SubDocument doc;
      bool doc_found = false;
      GetSubDocumentData data = { encoded_doc_key, &doc, &doc_found};
      data.low_index = &low_bound;
      data.high_index = &high_bound;

      RETURN_NOT_OK(GetAndPopulateResponseValues(
          iterator_.get(), AddResponseValuesSortedSets, data, ValueType::kObject, request_,
          &response_, add_keys, /* add_values */ true, reverse));
      break;
    }
    case RedisCollectionGetRangeRequestPB_GetRangeRequestType_UNKNOWN:
      return STATUS(InvalidCommand, "Unknown Collection Get Range Request not supported");
  }

  return Status::OK();
}

Result<RedisDataType> RedisReadOperation::GetValueType(int subkey_index) {
  return GetRedisValueType(iterator_.get(), request_.key_value(),
                           nullptr /* doc_write_batch */, subkey_index);
}

Result<RedisValue> RedisReadOperation::GetOverrideValue(int subkey_index) {
  return GetRedisValue(iterator_.get(), request_.key_value(),
                       subkey_index, /* always_override */ true);
}

Result<RedisValue> RedisReadOperation::GetValue(int subkey_index) {
    return GetRedisValue(iterator_.get(), request_.key_value(), subkey_index);
}

Status RedisReadOperation::ExecuteGetTtl() {
  const RedisKeyValuePB& kv = request_.key_value();
  if (!kv.has_key()) {
    return STATUS(Corruption, "Expected KeyValuePB");
  }
  // We currently only support getting and setting TTL on top level keys.
  if (!kv.subkey().empty()) {
    return STATUS_SUBSTITUTE(Corruption,
                             "Expected no subkeys, got $0", kv.subkey().size());
  }

  bool doc_found = false;
  Expiration exp;
  auto encoded_doc_key = DocKey::EncodedFromRedisKey(kv.hash_code(), kv.key());
  RETURN_NOT_OK(GetTtl(encoded_doc_key.AsSlice(), iterator_.get(), &doc_found, &exp));

  if (!doc_found) {
    response_.set_int_response(-2);
    return Status::OK();
  }

  if (exp.ttl.Equals(Value::kMaxTtl)) {
    response_.set_int_response(-1);
    return Status::OK();
  }

  MonoDelta ttl = VERIFY_RESULT(exp.ComputeRelativeTtl(iterator_->read_time().read));
  if (ttl.IsNegative()) {
    // The value has expired.
    response_.set_int_response(-2);
    return Status::OK();
  }

  response_.set_int_response(request_.get_ttl_request().return_seconds() ?
                             (int64_t) std::round(ttl.ToSeconds()) :
                             ttl.ToMilliseconds());
  return Status::OK();
}

Status RedisReadOperation::ExecuteGet() {
  const auto request_type = request_.get_request().request_type();
  RedisDataType expected_type;
  switch (request_type) {
    case RedisGetRequestPB_GetRequestType_GET:
      expected_type = REDIS_TYPE_STRING; break;
    case RedisGetRequestPB_GetRequestType_TSGET:
      expected_type = REDIS_TYPE_TIMESERIES; break;
    case RedisGetRequestPB_GetRequestType_HGET: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_HEXISTS:
      expected_type = REDIS_TYPE_HASH; break;
    case RedisGetRequestPB_GetRequestType_SISMEMBER:
      expected_type = REDIS_TYPE_SET; break;
    case RedisGetRequestPB_GetRequestType_ZSCORE:
      expected_type = REDIS_TYPE_SORTEDSET; break;
    default:
      expected_type = REDIS_TYPE_NONE;
  }
  switch (request_type) {
    case RedisGetRequestPB_GetRequestType_GET: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_TSGET: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_HGET: {
      auto type = GetValueType();
      RETURN_NOT_OK(type);
      // TODO: this is primarily glue for the Timeseries bug where the parent
      // may get compacted due to an outdated TTL even though the children
      // have longer TTL's and thus still exist. When fixing, take note that
      // GetValueType finds the value type of the parent, so if the parent
      // does not have the maximum TTL, it will return REDIS_TYPE_NONE when it
      // should not.
      if (expected_type == REDIS_TYPE_TIMESERIES && *type == REDIS_TYPE_NONE) {
        *type = expected_type;
      }
      // If wrong type, we set the error code in the response.
      if (VerifyTypeAndSetCode(expected_type, *type, &response_, VerifySuccessIfMissing::kTrue)) {
        auto value = request_type == RedisGetRequestPB_GetRequestType_TSGET ?
            GetOverrideValue() : GetValue();
        RETURN_NOT_OK(value);
        if (VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_,
            VerifySuccessIfMissing::kTrue)) {
          response_.set_string_response(value->value);
        }
      }
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_ZSCORE: {
      auto type = GetValueType();
      RETURN_NOT_OK(type);
      // If wrong type, we set the error code in the response.
      if (!VerifyTypeAndSetCode(expected_type, *type, &response_, VerifySuccessIfMissing::kTrue)) {
        return Status::OK();
      }
      SubDocKey key_reverse = SubDocKey(
          DocKey::FromRedisKey(request_.key_value().hash_code(), request_.key_value().key()),
          PrimitiveValue(ValueType::kSSReverse),
          PrimitiveValue(request_.key_value().subkey(0).string_subkey()));
      SubDocument subdoc_reverse;
      bool subdoc_reverse_found = false;
      auto encoded_key_reverse = key_reverse.EncodeWithoutHt();
      GetSubDocumentData get_data = { encoded_key_reverse, &subdoc_reverse, &subdoc_reverse_found };
      RETURN_NOT_OK(GetSubDocument(doc_db_, get_data, redis_query_id(),
                                   boost::none /* txn_op_context */, deadline_, read_time_));
      if (subdoc_reverse_found) {
        double score = subdoc_reverse.GetDouble();
        response_.set_string_response(std::to_string(score));
      } else {
        response_.set_code(RedisResponsePB_RedisStatusCode_NIL);
      }
      return Status::OK();
    }
    case RedisGetRequestPB_GetRequestType_HEXISTS: FALLTHROUGH_INTENDED;
    case RedisGetRequestPB_GetRequestType_SISMEMBER: {
      auto type = GetValueType();
      RETURN_NOT_OK(type);
      if (VerifyTypeAndSetCode(expected_type, *type, &response_, VerifySuccessIfMissing::kTrue)) {
        auto subtype = GetValueType(0);
        RETURN_NOT_OK(subtype);
        SetOptionalInt(*subtype, 1, &response_);
        response_.set_code(RedisResponsePB_RedisStatusCode_OK);
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
        response_.set_code(RedisResponsePB_RedisStatusCode_OK);
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
      const auto& req_kv = request_.key_value();
      size_t num_subkeys = req_kv.subkey_size();
      vector<int> indices(num_subkeys);
      for (int i = 0; i < num_subkeys; ++i) {
        indices[i] = i;
      }
      std::sort(indices.begin(), indices.end(), [&req_kv](int i, int j) {
            return req_kv.subkey(i).string_subkey() < req_kv.subkey(j).string_subkey();
          });

      string current_value = "";
      response_.mutable_array_response()->mutable_elements()->Reserve(num_subkeys);
      for (int i = 0; i < num_subkeys; ++i) {
        response_.mutable_array_response()->add_elements();
      }
      for (int i = 0; i < num_subkeys; ++i) {
        if (i == 0 ||
            req_kv.subkey(indices[i]).string_subkey() !=
            req_kv.subkey(indices[i - 1]).string_subkey()) {
          // If the condition above is false, we encountered the same key again, no need to call
          // GetValue() once more, current_value is already correct.
          auto value = GetValue(indices[i]);
          RETURN_NOT_OK(value);
          if (value->type == REDIS_TYPE_STRING) {
            current_value = std::move(value->value);
          } else {
            current_value = ""; // Empty string is nil response.
          }
        }
        *response_.mutable_array_response()->mutable_elements(indices[i]) = current_value;
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
    case RedisGetRequestPB_GetRequestType_TSCARD:
      return ExecuteHGetAllLikeCommands(ValueType::kRedisTS, false, false);
    case RedisGetRequestPB_GetRequestType_ZCARD:
      return ExecuteHGetAllLikeCommands(ValueType::kRedisSortedSet, false, false);
    case RedisGetRequestPB_GetRequestType_LLEN:
      return ExecuteHGetAllLikeCommands(ValueType::kRedisList, false, false);
    case RedisGetRequestPB_GetRequestType_UNKNOWN: {
      return STATUS(InvalidCommand, "Unknown Get Request not supported");
    }
  }
  return Status::OK();
}

Status RedisReadOperation::ExecuteStrLen() {
  auto value = GetValue();
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  RETURN_NOT_OK(value);

  if (VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_,
        VerifySuccessIfMissing::kTrue)) {
    SetOptionalInt(value->type, value->value.length(), &response_);
  }
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);

  return Status::OK();
}

Status RedisReadOperation::ExecuteExists() {
  auto value = GetValue();
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  RETURN_NOT_OK(value);

  // We only support exist command with one argument currently.
  SetOptionalInt(value->type, 1, &response_);
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);

  return Status::OK();
}

Status RedisReadOperation::ExecuteGetRange() {
  auto value = GetValue();
  RETURN_NOT_OK(value);

  if (!VerifyTypeAndSetCode(RedisDataType::REDIS_TYPE_STRING, value->type, &response_,
      VerifySuccessIfMissing::kTrue)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  const int32_t len = value->value.length();
  int32_t exclusive_end = request_.get_range_request().end() + 1;
  if (exclusive_end == 0) {
    exclusive_end = len;
  }

  // We treat negative indices to refer backwards from the end of the string.
  const int32_t start = ApplyIndex(request_.get_range_request().start(), len);
  int32_t end = ApplyIndex(exclusive_end, len);
  if (end < start) {
    end = start;
  }

  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  response_.set_string_response(value->value.c_str() + start, end - start);
  return Status::OK();
}

const RedisResponsePB& RedisReadOperation::response() {
  return response_;
}

//--------------------------------------------------------------------------------------------------
// CQL support.
//--------------------------------------------------------------------------------------------------
namespace {

// Append dummy entries in schema to table_row
// TODO(omer): this should most probably be added somewhere else
void AddProjection(const Schema& schema, QLTableRow* table_row) {
  for (size_t i = 0; i < schema.num_columns(); i++) {
    const auto& column_id = schema.column_id(i);
    table_row->AllocColumn(column_id);
  }
}

// Create projection schemas of static and non-static columns from a rowblock projection schema
// (for read) and a WHERE / IF condition (for read / write). "schema" is the full table schema
// and "rowblock_schema" is the selected columns from which we are splitting into static and
// non-static column portions.
CHECKED_STATUS CreateProjections(const Schema& schema, const QLReferencedColumnsPB& column_refs,
                                 Schema* static_projection, Schema* non_static_projection) {
  // The projection schemas are used to scan docdb.
  unordered_set<ColumnId> static_columns, non_static_columns;

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

CHECKED_STATUS PopulateRow(const QLTableRow& table_row, const Schema& schema,
                           const size_t begin_idx, const size_t col_count,
                           QLRow* row, size_t *col_idx) {
  for (size_t i = begin_idx; i < begin_idx + col_count; i++) {
    RETURN_NOT_OK(table_row.GetValue(schema.column_id(i), row->mutable_column((*col_idx)++)));
  }
  return Status::OK();
}

CHECKED_STATUS PopulateRow(const QLTableRow& table_row, const Schema& projection,
                           QLRow* row, size_t* col_idx) {
  return PopulateRow(table_row, projection, 0, projection.num_columns(), row, col_idx);
}

// Outer join a static row with a non-static row.
// A join is successful if and only if for every hash key, the values in the static and the
// non-static row are either non-NULL and the same, or one of them is NULL. Therefore we say that
// a join is successful if the static row is empty, and in turn return true.
// Copies the entries from the static row into the non-static one.
bool JoinStaticRow(
    const Schema& schema, const Schema& static_projection, const QLTableRow& static_row,
    QLTableRow* non_static_row) {
  // The join is successful if the static row is empty
  if (static_row.IsEmpty()) {
    return true;
  }

  // Now we know that the static row is not empty. The non-static row cannot be empty, therefore
  // we know that both the static row and the non-static one have non-NULL entries for all
  // hash keys. Therefore if MatchColumn returns false, we know the join is unsuccessful.
  // TODO(neil)
  // - Need to assign TTL and WriteTime to their default values.
  // - Check if they should be compared and copied over. Most likely not needed as we don't allow
  //   selecting TTL and WriteTime for static columns.
  // - This copying function should be moved to QLTableRow class.
  for (size_t i = 0; i < schema.num_hash_key_columns(); i++) {
    if (!non_static_row->MatchColumn(schema.column_id(i), static_row)) {
      return false;
    }
  }

  // Join the static columns in the static row into the non-static row.
  for (size_t i = 0; i < static_projection.num_columns(); i++) {
    CHECK_OK(non_static_row->CopyColumn(static_projection.column_id(i), static_row));
  }

  return true;
}

// Join a non-static row with a static row.
// Returns true if the two rows match
bool JoinNonStaticRow(
    const Schema& schema, const Schema& static_projection, const QLTableRow& non_static_row,
    QLTableRow* static_row) {
  bool join_successful = true;

  for (size_t i = 0; i < schema.num_hash_key_columns(); i++) {
    if (!static_row->MatchColumn(schema.column_id(i), non_static_row)) {
      join_successful = false;
      break;
    }
  }

  if (!join_successful) {
    static_row->Clear();
    for (size_t i = 0; i < static_projection.num_columns(); i++) {
      static_row->AllocColumn(static_projection.column_id(i));
    }

    for (size_t i = 0; i < schema.num_hash_key_columns(); i++) {
      CHECK_OK(static_row->CopyColumn(schema.column_id(i), non_static_row));
    }
  }
  return join_successful;
}

} // namespace

Status QLWriteOperation::Init(QLWriteRequestPB* request, QLResponsePB* response) {
  request_.Swap(request);
  response_ = response;
  insert_into_unique_index_ = request_.type() == QLWriteRequestPB::QL_STMT_INSERT &&
                              unique_index_key_schema_ != nullptr;
  require_read_ = RequireRead(request_, schema_) || insert_into_unique_index_;
  update_indexes_ = !request_.update_index_ids().empty();

  // Determine if static / non-static columns are being written.
  bool write_static_columns = false;
  bool write_non_static_columns = false;
  for (const auto& column : request_.column_values()) {
    auto schema_column = schema_.column_by_id(ColumnId(column.column_id()));
    RETURN_NOT_OK(schema_column);
    if (schema_column->is_static()) {
      write_static_columns = true;
    } else {
      write_non_static_columns = true;
    }
    if (write_static_columns && write_non_static_columns) {
      break;
    }
  }

  bool is_range_operation = IsRangeOperation(request_, schema_);

  // We need the hashed key if writing to the static columns, and need primary key if writing to
  // non-static columns or writing the full primary key (i.e. range columns are present or table
  // does not have range columns).
  return InitializeKeys(
      write_static_columns || is_range_operation,
      write_non_static_columns || !request_.range_column_values().empty() ||
          schema_.num_range_key_columns() == 0);
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
                                     QLTableRow* table_row) {
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
                                data.doc_write_batch->doc_db(),
                                data.deadline, data.read_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(table_row));
    }
    data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
  }
  if (pk_doc_key_ != nullptr) {
    DocQLScanSpec spec(*non_static_projection, *pk_doc_key_, request_.query_id());
    DocRowwiseIterator iterator(*non_static_projection, schema_, txn_op_context_,
                                data.doc_write_batch->doc_db(),
                                data.deadline, data.read_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(table_row));
      // If there are indexes to update, check if liveness column exists for update/delete because
      // that will affect whether the row will still exist after the DML and whether we need to
      // remove the key from the indexes.
      if (update_indexes_ && (request_.type() == QLWriteRequestPB::QL_STMT_UPDATE ||
                              request_.type() == QLWriteRequestPB::QL_STMT_DELETE)) {
        liveness_column_exists_ = iterator.LivenessColumnExists();
      }
    } else {
      // If no non-static column is found, the row does not exist and we should clear the static
      // columns in the map to indicate the row does not exist.
      table_row->Clear();
    }
    data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
  }

  return Status::OK();
}

Status QLWriteOperation::PopulateConditionalDmlRow(const DocOperationApplyData& data,
                                                   const bool should_apply,
                                                   const QLTableRow& table_row,
                                                   Schema static_projection,
                                                   Schema non_static_projection,
                                                   std::unique_ptr<QLRowBlock>* rowblock) {
  // Populate the result set to return the "applied" status, and optionally the hash / primary key
  // and the present column values if the condition is not satisfied and the row does exist
  // (value_map is not empty).
  const bool return_present_values = !should_apply && !table_row.IsEmpty();
  const size_t num_key_columns = pk_doc_key_ ? schema_.num_key_columns()
      : schema_.num_hash_key_columns();
  std::vector<ColumnSchema> columns;
  columns.emplace_back(ColumnSchema("[applied]", BOOL));
  if (return_present_values) {
    columns.insert(columns.end(), schema_.columns().begin(),
        schema_.columns().begin() + num_key_columns);
    columns.insert(columns.end(), static_projection.columns().begin(),
        static_projection.columns().end());
    columns.insert(columns.end(), non_static_projection.columns().begin(),
        non_static_projection.columns().end());
  }
  rowblock->reset(new QLRowBlock(Schema(columns, 0)));
  QLRow& row = rowblock->get()->Extend();
  row.mutable_column(0)->set_bool_value(should_apply);
  size_t col_idx = 1;
  if (return_present_values) {
    RETURN_NOT_OK(PopulateRow(table_row, schema_, 0, num_key_columns, &row, &col_idx));
    RETURN_NOT_OK(PopulateRow(table_row, static_projection, &row, &col_idx));
    RETURN_NOT_OK(PopulateRow(table_row, non_static_projection, &row, &col_idx));
  }

  return Status::OK();
}

Status QLWriteOperation::PopulateStatusRow(const DocOperationApplyData& data,
                                           const bool should_apply,
                                           const QLTableRow& table_row,
                                           std::unique_ptr<QLRowBlock>* rowblock) {

  std::vector<ColumnSchema> columns;
  columns.emplace_back(ColumnSchema("[applied]", BOOL));
  columns.emplace_back(ColumnSchema("[message]", STRING));
  columns.insert(columns.end(), schema_.columns().begin(), schema_.columns().end());

  rowblock->reset(new QLRowBlock(Schema(columns, 0)));
  QLRow& row = rowblock->get()->Extend();
  row.mutable_column(0)->set_bool_value(should_apply);
  // No message unless there is an error (then message will be set in executor).

  // If not applied report the existing row values as for regular if clause.
  if (!should_apply) {
    for (size_t i = 0; i < schema_.num_columns(); i++) {
      boost::optional<const QLValuePB&> col_val = table_row.GetValue(schema_.column_id(i));
      if (col_val.is_initialized()) {
        *(row.mutable_column(i + 2)) = *col_val;
      }
    }
  }

  return Status::OK();
}

// Check if a duplicate value is inserted into a unique index.
Result<bool> QLWriteOperation::DuplicateUniqueIndexValue(const DocOperationApplyData& data) {
  // Set up the iterator to read the current primary key associated with the index key.
  DocQLScanSpec spec(*unique_index_key_schema_, *pk_doc_key_, request_.query_id());
  DocRowwiseIterator iterator(*unique_index_key_schema_, schema_, txn_op_context_,
                              data.doc_write_batch->doc_db(), data.deadline, data.read_time);
  RETURN_NOT_OK(iterator.Init(spec));

  // It is a duplicate value if the index key exist already and the associated indexed primary key
  // is not the same.
  if (!iterator.HasNext()) {
    return false;
  }
  QLTableRow table_row;
  RETURN_NOT_OK(iterator.NextRow(&table_row));
  std::unordered_set<ColumnId> key_column_ids(unique_index_key_schema_->column_ids().begin(),
                                              unique_index_key_schema_->column_ids().end());
  for (const auto& column_value : request_.column_values()) {
    ColumnId column_id(column_value.column_id());
    if (key_column_ids.count(column_id) > 0) {
      auto value = table_row.GetValue(column_id);
      if (value && *value != column_value.expr().value()) {
        return true;
      }
    }
  }

  return false;
}

Status QLWriteOperation::ApplyForJsonOperators(const QLColumnValuePB& column_value,
                                               const DocOperationApplyData& data,
                                               const DocPath& sub_path, const MonoDelta& ttl,
                                               const UserTimeMicros& user_timestamp,
                                               const ColumnSchema& column,
                                               QLTableRow* existing_row) {
  // Read the json column value inorder to perform a read modify write.
  QLValue ql_value;
  RETURN_NOT_OK(existing_row->ReadColumn(column_value.column_id(), &ql_value));
  Jsonb jsonb(std::move(ql_value.jsonb_value()));
  rapidjson::Document document;
  RETURN_NOT_OK(jsonb.ToRapidJson(&document));

  // Deserialize the rhs.
  Jsonb rhs(std::move(column_value.expr().value().jsonb_value()));
  rapidjson::Document rhs_doc;
  RETURN_NOT_OK(rhs.ToRapidJson(&rhs_doc));

  // Update the json value.
  rapidjson::Value::MemberIterator memberit;
  rapidjson::Value::ValueIterator valueit;
  bool last_elem_object = true;
  RETURN_NOT_OK(FindMemberForIndex(column_value, 0, &document, &memberit, &valueit,
                                   &last_elem_object));
  for (int i = 1; i < column_value.json_args_size(); i++) {
    if (last_elem_object) {
      RETURN_NOT_OK(FindMemberForIndex(column_value, i, &(memberit->value), &memberit, &valueit,
                                       &last_elem_object));
    } else {
      RETURN_NOT_OK(FindMemberForIndex(column_value, i, &(*valueit), &memberit, &valueit,
                                       &last_elem_object));
    }
  }
  if (last_elem_object) {
    memberit->value = rhs_doc.Move();
  } else {
    *valueit = rhs_doc.Move();
  }

  // Now write the new json value back.
  QLValue result;
  Jsonb jsonb_result;
  RETURN_NOT_OK(jsonb_result.FromRapidJson(document));
  *result.mutable_jsonb_value() = std::move(jsonb_result.MoveSerializedJsonb());
  const SubDocument& sub_doc =
      SubDocument::FromQLValuePB(result.value(), column.sorting_type(),
                                 TSOpcode::kScalarInsert);
  RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
    sub_path, sub_doc, data.read_time, data.deadline,
    request_.query_id(), ttl, user_timestamp));

  // Update the current row as well so that we can accumulate the result of multiple json
  // operations and write the final value.
  existing_row->AllocColumn(column_value.column_id()).value = result.value();
  return Status::OK();
}

Status QLWriteOperation::ApplyForSubscriptArgs(const QLColumnValuePB& column_value,
                                               const QLTableRow& existing_row,
                                               const DocOperationApplyData& data,
                                               const MonoDelta& ttl,
                                               const UserTimeMicros& user_timestamp,
                                               const ColumnSchema& column,
                                               DocPath* sub_path) {
  QLValue expr_result;
  RETURN_NOT_OK(EvalExpr(column_value.expr(), existing_row, &expr_result));
  const TSOpcode write_instr = GetTSWriteInstruction(column_value.expr());
  const SubDocument& sub_doc =
      SubDocument::FromQLValuePB(expr_result.value(), column.sorting_type(), write_instr);
  RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));

  // Setting the value for a sub-column
  // Currently we only support two cases here: `map['key'] = v` and `list[index] = v`)
  // Any other case should be rejected by the semantic analyser before getting here
  // Later when we support frozen or nested collections this code may need refactoring
  DCHECK_EQ(column_value.subscript_args().size(), 1);
  DCHECK(column_value.subscript_args(0).has_value()) << "An index must be a constant";
  switch (column.type()->main()) {
    case MAP: {
      const PrimitiveValue &pv = PrimitiveValue::FromQLValuePB(
          column_value.subscript_args(0).value(),
          ColumnSchema::SortingType::kNotSpecified);
      sub_path->AddSubKey(pv);
      RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
          *sub_path, sub_doc, data.read_time, data.deadline,
          request_.query_id(), ttl, user_timestamp));
      break;
    }
    case LIST: {
      MonoDelta default_ttl = schema_.table_properties().HasDefaultTimeToLive() ?
          MonoDelta::FromMilliseconds(schema_.table_properties().DefaultTimeToLive()) :
          MonoDelta::kMax;

      // At YQL layer list indexes start at 0, but internally we start at 1.
      int index = column_value.subscript_args(0).value().int32_value() + 1;
      RETURN_NOT_OK(data.doc_write_batch->ReplaceCqlInList(
          *sub_path, {index}, {sub_doc}, data.read_time, data.deadline, request_.query_id(),
          default_ttl, ttl));
      break;
    }
    default: {
      LOG(ERROR) << "Unexpected type for setting subcolumn: "
                 << column.type()->ToString();
    }
  }
  return Status::OK();
}

Status QLWriteOperation::ApplyForRegularColumns(const QLColumnValuePB& column_value,
                                                const QLTableRow& existing_row,
                                                const DocOperationApplyData& data,
                                                const DocPath& sub_path, const MonoDelta& ttl,
                                                const UserTimeMicros& user_timestamp,
                                                const ColumnSchema& column,
                                                const ColumnId& column_id,
                                                QLTableRow* new_row) {
  // Typical case, setting a columns value
  QLValue expr_result;
  RETURN_NOT_OK(EvalExpr(column_value.expr(), existing_row, &expr_result));
  const TSOpcode write_instr = GetTSWriteInstruction(column_value.expr());
  const SubDocument& sub_doc =
      SubDocument::FromQLValuePB(expr_result.value(), column.sorting_type(), write_instr);
  switch (write_instr) {
    case TSOpcode::kScalarInsert:
          RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
              sub_path, sub_doc, data.read_time, data.deadline,
              request_.query_id(), ttl, user_timestamp));
      break;
    case TSOpcode::kMapExtend:
    case TSOpcode::kSetExtend:
    case TSOpcode::kMapRemove:
    case TSOpcode::kSetRemove:
          RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));
          RETURN_NOT_OK(data.doc_write_batch->ExtendSubDocument(
            sub_path, sub_doc, data.read_time, data.deadline, request_.query_id(), ttl));
      break;
    case TSOpcode::kListPrepend:
          sub_doc.SetExtendOrder(ListExtendOrder::PREPEND_BLOCK);
          FALLTHROUGH_INTENDED;
    case TSOpcode::kListAppend:
          RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));
          RETURN_NOT_OK(data.doc_write_batch->ExtendList(
              sub_path, sub_doc, data.read_time, data.deadline, request_.query_id(), ttl));
      break;
    case TSOpcode::kListRemove:
      // TODO(akashnil or mihnea) this should call RemoveFromList once thats implemented
      // Currently list subtraction is computed in memory using builtin call so this
      // case should never be reached. Once it is implemented the corresponding case
      // from EvalQLExpressionPB should be uncommented to enable this optimization.
          RETURN_NOT_OK(CheckUserTimestampForCollections(user_timestamp));
          RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
              sub_path, sub_doc, data.read_time, data.deadline,
              request_.query_id(), ttl, user_timestamp));
      break;
    default:
      LOG(FATAL) << "Unsupported operation: " << static_cast<int>(write_instr);
      break;
  }

  if (update_indexes_) {
    new_row->AllocColumn(column_id, expr_result);
  }
  return Status::OK();
}

Status QLWriteOperation::Apply(const DocOperationApplyData& data) {
  QLTableRow existing_row;
  if (request_.has_if_expr()) {
    // Check if the if-condition is satisfied.
    bool should_apply = true;
    Schema static_projection, non_static_projection;
    RETURN_NOT_OK(ReadColumns(data, &static_projection, &non_static_projection, &existing_row));
    RETURN_NOT_OK(EvalCondition(request_.if_expr().condition(), existing_row, &should_apply));
    // Set the response accordingly.
    response_->set_applied(should_apply);
    if (!should_apply && request_.else_error()) {
      return STATUS(QLError, "Condition was not satisfied.");
    } else if (request_.returns_status()) {
      RETURN_NOT_OK(PopulateStatusRow(data, should_apply, existing_row, &rowblock_));
    } else {
      RETURN_NOT_OK(PopulateConditionalDmlRow(data,
          should_apply,
          existing_row,
          static_projection,
          non_static_projection,
          &rowblock_));
    }

    // If we do not need to apply we are already done.
    if (!should_apply) {
      response_->set_status(QLResponsePB::YQL_STATUS_OK);
      return Status::OK();
    }
  } else if (RequireReadForExpressions(request_) || request_.returns_status()) {
    RETURN_NOT_OK(ReadColumns(data, nullptr, nullptr, &existing_row));
    if (request_.returns_status()) {
      RETURN_NOT_OK(PopulateStatusRow(data, /* should_apply = */ true, existing_row, &rowblock_));
    }
  }

  if (insert_into_unique_index_ && VERIFY_RESULT(DuplicateUniqueIndexValue(data))) {
    response_->set_applied(false);
    response_->set_status(QLResponsePB::YQL_STATUS_OK);
    return Status::OK();
  }

  const MonoDelta ttl =
      request_.has_ttl() ? MonoDelta::FromMilliseconds(request_.ttl()) : Value::kMaxTtl;

  const UserTimeMicros user_timestamp = request_.has_user_timestamp_usec() ?
      request_.user_timestamp_usec() : Value::kInvalidUserTimestamp;

  // Initialize the new row being written to either the existing row if read, or just populate
  // the primary key.
  QLTableRow new_row;
  if (!existing_row.IsEmpty()) {
    new_row = existing_row;
  } else {
    size_t idx = 0;
    for (const QLExpressionPB& expr : request_.hashed_column_values()) {
      new_row.AllocColumn(schema_.column_id(idx), expr.value());
      idx++;
    }
    for (const QLExpressionPB& expr : request_.range_column_values()) {
      new_row.AllocColumn(schema_.column_id(idx), expr.value());
      idx++;
    }
  }

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
            sub_path, value, data.read_time, data.deadline, request_.query_id()));
      }

      for (const auto& column_value : request_.column_values()) {
        if (!column_value.has_column_id()) {
          return STATUS_FORMAT(InvalidArgument, "column id missing: $0",
                               column_value.DebugString());
        }
        const ColumnId column_id(column_value.column_id());
        const auto maybe_column = schema_.column_by_id(column_id);
        RETURN_NOT_OK(maybe_column);
        const ColumnSchema& column = *maybe_column;

        DocPath sub_path(column.is_static() ? hashed_doc_path_->encoded_doc_key()
                                            : pk_doc_path_->encoded_doc_key(),
                         PrimitiveValue(column_id));

        QLValue expr_result;
        if (!column_value.json_args().empty()) {
          RETURN_NOT_OK(ApplyForJsonOperators(column_value, data, sub_path, ttl,
                                              user_timestamp, column, &new_row));
        } else if (!column_value.subscript_args().empty()) {
          RETURN_NOT_OK(ApplyForSubscriptArgs(column_value, existing_row, data, ttl,
                                              user_timestamp, column, &sub_path));
        } else {
          RETURN_NOT_OK(ApplyForRegularColumns(column_value, existing_row, data, sub_path, ttl,
                                               user_timestamp, column, column_id, &new_row));
        }
      }

      if (update_indexes_) {
        RETURN_NOT_OK(UpdateIndexes(existing_row, new_row));
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
          const auto column = schema_.column_by_id(column_id);
          RETURN_NOT_OK(column);
          const DocPath sub_path(
              column->is_static() ? hashed_doc_path_->encoded_doc_key()
                                  : pk_doc_path_->encoded_doc_key(),
              PrimitiveValue(column_id));
          RETURN_NOT_OK(data.doc_write_batch->DeleteSubDoc(sub_path,
              data.read_time, data.deadline, request_.query_id(), user_timestamp));
          if (update_indexes_) {
            new_row.ClearValue(column_id);
          }
        }
        if (update_indexes_) {
          RETURN_NOT_OK(UpdateIndexes(existing_row, new_row));
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

        DocQLScanSpec spec(projection, request_.hash_code(), boost::none, hashed_components,
            request_.has_where_expr() ? &request_.where_expr().condition() : nullptr,
            request_.query_id());

        // Create iterator.
        DocRowwiseIterator iterator(
            projection, schema_, txn_op_context_,
            data.doc_write_batch->doc_db(),
            data.deadline, data.read_time);
        RETURN_NOT_OK(iterator.Init(spec));

        // Iterate through rows and delete those that match the condition.
        // TODO We do not lock here, so other write transactions coming in might appear partially
        // applied if they happen in the middle of a ranged delete.
        while (iterator.HasNext()) {
          existing_row.Clear();
          RETURN_NOT_OK(iterator.NextRow(&existing_row));

          // Match the row with the where condition before deleting it.
          bool match = false;
          RETURN_NOT_OK(spec.Match(existing_row, &match));
          if (match) {
            const DocKey& row_key = iterator.row_key();
            const DocPath row_path(row_key.Encode());
            RETURN_NOT_OK(DeleteRow(row_path, data.doc_write_batch, data.read_time, data.deadline));
            if (update_indexes_) {
              liveness_column_exists_ = iterator.LivenessColumnExists();
              RETURN_NOT_OK(UpdateIndexes(existing_row, new_row));
            }
          }
        }
        data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
      } else {
        // Otherwise, delete the referenced row (all columns).
        RETURN_NOT_OK(DeleteRow(*pk_doc_path_, data.doc_write_batch,
                                data.read_time, data.deadline));
        if (update_indexes_) {
          RETURN_NOT_OK(UpdateIndexes(existing_row, new_row));
        }
      }
      break;
    }
  }

  response_->set_status(QLResponsePB::YQL_STATUS_OK);

  return Status::OK();
}

Status QLWriteOperation::DeleteRow(const DocPath& row_path, DocWriteBatch* doc_write_batch,
                                   const ReadHybridTime& read_ht, const MonoTime deadline) {
  if (request_.has_user_timestamp_usec()) {
    // If user_timestamp is provided, we need to add a tombstone for each individual
    // column in the schema since we don't want to analyze this on the read path.
    for (int i = schema_.num_key_columns(); i < schema_.num_columns(); i++) {
      const DocPath sub_path(row_path.encoded_doc_key(),
                             PrimitiveValue(schema_.column_id(i)));
      RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(sub_path,
                                                  read_ht,
                                                  deadline,
                                                  request_.query_id(),
                                                  request_.user_timestamp_usec()));
    }

    // Delete the liveness column as well.
    const DocPath liveness_column(
        row_path.encoded_doc_key(),
        PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn));
    RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(liveness_column,
                                                read_ht,
                                                deadline,
                                                request_.query_id(),
                                                request_.user_timestamp_usec()));
  } else {
    RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(row_path, read_ht, deadline));
  }

  return Status::OK();
}

namespace {

YB_DEFINE_ENUM(ValueState, (kNull)(kNotNull)(kMissing));

ValueState GetValueState(const QLTableRow& row, const ColumnId column_id) {
  const auto value = row.GetValue(column_id);
  return !value ? ValueState::kMissing : IsNull(*value) ? ValueState::kNull : ValueState::kNotNull;
}

} // namespace

bool QLWriteOperation::IsRowDeleted(const QLTableRow& existing_row,
                                    const QLTableRow& new_row) const {
  // Delete the whole row?
  if (request_.type() == QLWriteRequestPB::QL_STMT_DELETE && request_.column_values().empty()) {
    return true;
  }

  // For update/delete, if there is no liveness column, the row will be deleted after the DML unless
  // a non-null column still remains.
  if ((request_.type() == QLWriteRequestPB::QL_STMT_UPDATE ||
       request_.type() == QLWriteRequestPB::QL_STMT_DELETE) &&
      !liveness_column_exists_) {
    for (size_t idx = schema_.num_key_columns(); idx < schema_.num_columns(); idx++) {
      if (schema_.column(idx).is_static()) {
        continue;
      }
      const ColumnId column_id = schema_.column_id(idx);
      switch (GetValueState(new_row, column_id)) {
        case ValueState::kNull: continue;
        case ValueState::kNotNull: return false;
        case ValueState::kMissing: break;
      }
      switch (GetValueState(existing_row, column_id)) {
        case ValueState::kNull: continue;
        case ValueState::kNotNull: return false;
        case ValueState::kMissing: break;
      }
    }
    return true;
  }

  return false;
}

namespace {

QLExpressionPB* NewKeyColumn(QLWriteRequestPB* request, const IndexInfo& index, const size_t idx) {
  return (idx < index.hash_column_count()
          ? request->add_hashed_column_values()
          : request->add_range_column_values());
}

} // namespace

QLWriteRequestPB* QLWriteOperation::NewIndexRequest(const IndexInfo* index,
                                                    const QLWriteRequestPB::QLStmtType type,
                                                    const QLTableRow& new_row) {
  index_requests_.emplace_back(index, QLWriteRequestPB());
  QLWriteRequestPB* request = &index_requests_.back().second;
  request->set_type(type);
  return request;
}

Status QLWriteOperation::UpdateIndexes(const QLTableRow& existing_row, const QLTableRow& new_row) {
  // Prepare the write requests to update the indexes. There should be at most 2 requests for each
  // index (one insert and one delete).
  const auto& index_ids = request_.update_index_ids();
  index_requests_.reserve(index_ids.size() * 2);
  for (const TableId& index_id : index_ids) {
    const IndexInfo* index = VERIFY_RESULT(index_map_.FindIndex(index_id));
    bool index_key_changed = false;
    if (IsRowDeleted(existing_row, new_row)) {
      index_key_changed = true;
    } else {
      QLWriteRequestPB* index_request = NewIndexRequest(index, QLWriteRequestPB::QL_STMT_INSERT,
                                                        new_row);
      // Prepare the new index key.
      for (size_t idx = 0; idx < index->key_column_count(); idx++) {
        const IndexInfo::IndexColumn& index_column = index->column(idx);
        QLExpressionPB *key_column = NewKeyColumn(index_request, *index, idx);
        auto result = new_row.GetValue(index_column.indexed_column_id);
        if (!existing_row.IsEmpty()) {
          // For each column in the index key, if there is a new value, see if the value is changed
          // from the current value. Else, use the current value.
          if (result) {
            if (!new_row.MatchColumn(index_column.indexed_column_id, existing_row)) {
              index_key_changed = true;
            }
          } else {
            result = existing_row.GetValue(index_column.indexed_column_id);
          }
        }
        if (result) {
          key_column->mutable_value()->CopyFrom(*result);
        }
      }
      // Prepare the covering columns.
      for (size_t idx = index->key_column_count(); idx < index->columns().size(); idx++) {
        const IndexInfo::IndexColumn& index_column = index->column(idx);
        auto result = new_row.GetValue(index_column.indexed_column_id);
        // If the index value is changed and there is no new covering column value set, use the
        // current value.
        if (index_key_changed && !result) {
          result = existing_row.GetValue(index_column.indexed_column_id);
        }
        if (result) {
          QLColumnValuePB* covering_column = index_request->add_column_values();
          covering_column->set_column_id(index_column.column_id);
          covering_column->mutable_expr()->mutable_value()->CopyFrom(*result);
        }
      }
    }
    // If the index key is changed, delete the current key.
    if (index_key_changed) {
      QLWriteRequestPB* index_request = NewIndexRequest(index, QLWriteRequestPB::QL_STMT_DELETE,
                                                        new_row);
      for (size_t idx = 0; idx < index->key_column_count(); idx++) {
        const IndexInfo::IndexColumn& index_column = index->column(idx);
        QLExpressionPB *key_column = NewKeyColumn(index_request, *index, idx);
        auto result = existing_row.GetValue(index_column.indexed_column_id);
        if (result) {
          key_column->mutable_value()->CopyFrom(*result);
        }
      }
    }
  }
  return Status::OK();
}

Status QLReadOperation::Execute(const common::YQLStorageIf& ql_storage,
                                MonoTime deadline,
                                const ReadHybridTime& read_time,
                                const Schema& schema,
                                const Schema& query_schema,
                                QLResultSet* resultset,
                                HybridTime* restart_read_ht) {
  size_t row_count_limit = std::numeric_limits<std::size_t>::max();
  size_t num_rows_skipped = 0;
  size_t offset = 0;
  if (request_.has_offset()) {
    offset = request_.offset();
  }
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

  std::unique_ptr<common::YQLRowwiseIteratorIf> iter;
  std::unique_ptr<common::QLScanSpec> spec, static_row_spec;
  ReadHybridTime req_read_time;
  RETURN_NOT_OK(ql_storage.BuildYQLScanSpec(
      request_, read_time, schema, read_static_columns, static_projection, &spec,
      &static_row_spec, &req_read_time));
  RETURN_NOT_OK(ql_storage.GetIterator(request_, query_schema, schema, txn_op_context_,
                                       deadline, req_read_time, *spec, &iter));
  if (FLAGS_trace_docdb_calls) {
    TRACE("Initialized iterator");
  }

  QLTableRow static_row;
  QLTableRow non_static_row;
  QLTableRow& selected_row = read_distinct_columns ? static_row : non_static_row;

  // In case when we are continuing a select with a paging state, or when using a reverse scan,
  // the static columns for the next row to fetch are not included in the first iterator and we
  // need to fetch them with a separate spec and iterator before beginning the normal fetch below.
  if (static_row_spec != nullptr) {
    std::unique_ptr<common::YQLRowwiseIteratorIf> static_row_iter;
    RETURN_NOT_OK(ql_storage.GetIterator(
        request_, static_projection, schema, txn_op_context_, deadline, req_read_time,
        *static_row_spec, &static_row_iter));
    if (static_row_iter->HasNext()) {
      RETURN_NOT_OK(static_row_iter->NextRow(&static_row));
    }
  }

  // Begin the normal fetch.
  int match_count = 0;
  bool static_dealt_with = true;
  while (resultset->rsrow_count() < row_count_limit && iter->HasNext()) {
    const bool last_read_static = iter->IsNextStaticColumn();

    // Note that static columns are sorted before non-static columns in DocDB as follows. This is
    // because "<empty_range_components>" is empty and terminated by kGroupEnd which sorts before
    // all other ValueType characters in a non-empty range component.
    //   <hash_code><hash_components><empty_range_components><static_column_id> -> value;
    //   <hash_code><hash_components><range_components><non_static_column_id> -> value;
    if (last_read_static) {
      static_row.Clear();
      RETURN_NOT_OK(iter->NextRow(static_projection, &static_row));
    } else { // Reading a regular row that contains non-static columns.

      // Read this regular row.
      // TODO(omer): this is quite inefficient if read_distinct_column. A better way to do this
      // would be to only read the first non-static column for each hash key, and skip the rest
      non_static_row.Clear();
      RETURN_NOT_OK(iter->NextRow(non_static_projection, &non_static_row));
    }

    // We have two possible cases: whether we use distinct or not
    // If we use distinct, then in general we only need to add the static rows
    // However, we might have to add non-static rows, if there is no static row corresponding to
    // it. Of course, we add one entry per hash key in non-static row.
    // If we do not use distinct, we are generally only adding non-static rows
    // However, if there is no non-static row for the static row, we have to add it.
    if (read_distinct_columns) {
      bool join_successful = false;
      if (!last_read_static) {
        join_successful = JoinNonStaticRow(schema, static_projection, non_static_row, &static_row);
      }

      // If the join was not successful, it means that the non-static row we read has no
      // corresponding static row, so we have to add it to the result
      if (!join_successful) {
        RETURN_NOT_OK(AddRowToResult(
            spec, static_row, row_count_limit, offset, resultset, &match_count, &num_rows_skipped));
      }
    } else {
      if (last_read_static) {

        // If the next row to be read is not static, deal with it later, as we do not know whether
        // the non-static row corresponds to this static row; if the non-static row doesn't
        // correspond to this static row, we will have to add it later, so set static_dealt_with to
        // false
        if (iter->HasNext() && !iter->IsNextStaticColumn()) {
          static_dealt_with = false;
          continue;
        }

        AddProjection(non_static_projection, &static_row);
        RETURN_NOT_OK(AddRowToResult(spec, static_row, row_count_limit, offset, resultset,
                                     &match_count, &num_rows_skipped));
      } else {
        // We also have to do the join if we are not reading any static columns, as Cassandra
        // reports nulls for static rows with no corresponding non-static row
        if (read_static_columns || !static_dealt_with) {
          const bool join_successful = JoinStaticRow(schema,
                                               static_projection,
                                               static_row,
                                               &non_static_row);
          // Add the static row if the join was not successful and it is the first time we are
          // dealing with this static row
          if (!join_successful && !static_dealt_with) {
            AddProjection(non_static_projection, &static_row);
            RETURN_NOT_OK(AddRowToResult(
                spec, static_row, row_count_limit, offset, resultset, &match_count,
                &num_rows_skipped));
          }
        }
        static_dealt_with = true;
        RETURN_NOT_OK(AddRowToResult(
            spec, non_static_row, row_count_limit, offset, resultset, &match_count,
            &num_rows_skipped));
      }
    }
  }

  if (request_.is_aggregate() && match_count > 0) {
    RETURN_NOT_OK(PopulateAggregate(selected_row, resultset));
  }

  if (FLAGS_trace_docdb_calls) {
    TRACE("Fetched $0 rows.", resultset->rsrow_count());
  }
  *restart_read_ht = iter->RestartReadHt();

  if ((resultset->rsrow_count() >= row_count_limit || request_.has_offset()) &&
      !request_.is_aggregate()) {
    RETURN_NOT_OK(iter->SetPagingStateIfNecessary(request_, num_rows_skipped, &response_));
  }

  return Status::OK();
}

CHECKED_STATUS QLReadOperation::PopulateResultSet(const QLTableRow& table_row,
                                                  QLResultSet *resultset) {
  resultset->AllocateRow();
  int rscol_index = 0;
  for (const QLExpressionPB& expr : request_.selected_exprs()) {
    QLValue value;
    RETURN_NOT_OK(EvalExpr(expr, table_row, &value));
    resultset->AppendColumn(rscol_index, value);
    rscol_index++;
  }

  return Status::OK();
}

CHECKED_STATUS QLReadOperation::EvalAggregate(const QLTableRow& table_row) {
  if (aggr_result_.empty()) {
    int column_count = request_.selected_exprs().size();
    aggr_result_.resize(column_count);
  }

  int aggr_index = 0;
  for (const QLExpressionPB& expr : request_.selected_exprs()) {
    RETURN_NOT_OK(EvalExpr(expr, table_row, &aggr_result_[aggr_index]));
    aggr_index++;
  }
  return Status::OK();
}

CHECKED_STATUS QLReadOperation::PopulateAggregate(const QLTableRow& table_row,
                                                  QLResultSet *resultset) {
  resultset->AllocateRow();
  int column_count = request_.selected_exprs().size();
  for (int rscol_index = 0; rscol_index < column_count; rscol_index++) {
    resultset->AppendColumn(rscol_index, aggr_result_[rscol_index]);
  }
  return Status::OK();
}

CHECKED_STATUS QLReadOperation::AddRowToResult(const std::unique_ptr<common::QLScanSpec>& spec,
                                               const QLTableRow& row,
                                               const size_t row_count_limit,
                                               const size_t offset,
                                               QLResultSet* resultset,
                                               int* match_count,
                                               size_t *num_rows_skipped) {
  if (resultset->rsrow_count() < row_count_limit) {
    bool match = false;
    RETURN_NOT_OK(spec->Match(row, &match));
    if (match) {
      if (*num_rows_skipped >= offset) {
        (*match_count)++;
        if (request_.is_aggregate()) {
          RETURN_NOT_OK(EvalAggregate(row));
        } else {
          RETURN_NOT_OK(PopulateResultSet(row, resultset));
        }
      } else {
        (*num_rows_skipped)++;
      }
    }
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Pgsql support.
//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgsqlDocOperation::CreateProjections(const Schema& schema,
                                                    const PgsqlColumnRefsPB& column_refs,
                                                    Schema* column_projection) {
  // The projection schemas are used to scan docdb. Keep the columns to fetch in sorted order for
  // more efficient scan in the iterator.
  set<ColumnId> regular_columns;

  // Add regular columns.
  for (int32_t id : column_refs.ids()) {
    const ColumnId column_id(id);
    if (!schema.is_key_column(column_id)) {
      regular_columns.insert(column_id);
    }
  }

  RETURN_NOT_OK(schema.CreateProjectionByIdsIgnoreMissing(
      vector<ColumnId>(regular_columns.begin(), regular_columns.end()),
      column_projection));

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgsqlWriteOperation::Init(PgsqlWriteRequestPB* request, PgsqlResponsePB* response) {
  // Initialize operation inputs.
  request_.Swap(request);
  response_ = response;

  // Init DocDB keys using partition and range values.
  // - Collect partition and range values into hashed_components and range_components.
  // - Setup the keys.
  vector<PrimitiveValue> hashed_components;
  RETURN_NOT_OK(InitKeyColumnPrimitiveValues(request_.partition_column_values(),
                                             schema_,
                                             0,
                                             &hashed_components));

  // We only need the hash key if the range key is not specified.
  if (request_.range_column_values().size() == 0) {
    hashed_doc_key_ = make_unique<DocKey>(request_.hash_code(), hashed_components);
    hashed_doc_path_ = make_unique<DocPath>(hashed_doc_key_->Encode());
  }

  vector<PrimitiveValue> range_components;
  RETURN_NOT_OK(InitKeyColumnPrimitiveValues(request_.range_column_values(),
                                             schema_,
                                             schema_.num_hash_key_columns(),
                                             &range_components));
  range_doc_key_ = make_unique<DocKey>(request_.hash_code(), hashed_components, range_components);
  range_doc_path_ = make_unique<DocPath>(range_doc_key_->Encode());

  return Status::OK();
}

CHECKED_STATUS PgsqlWriteOperation::Apply(const DocOperationApplyData& data) {
  switch (request_.stmt_type()) {
    case PgsqlWriteRequestPB::PGSQL_INSERT:
      return ApplyInsert(data);

    case PgsqlWriteRequestPB::PGSQL_UPDATE:
      return ApplyUpdate(data);

    case PgsqlWriteRequestPB::PGSQL_DELETE:
      return ApplyDelete(data);
  }
  return Status::OK();
}

CHECKED_STATUS PgsqlWriteOperation::ApplyInsert(const DocOperationApplyData& data) {
  QLTableRow::SharedPtr table_row = make_shared<QLTableRow>();
  if (RequireReadSnapshot()) {
    RETURN_NOT_OK(ReadColumns(data, table_row));
  }

  const MonoDelta ttl = Value::kMaxTtl;
  const UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp;

  // Add the appropriate liveness column.
  if (range_doc_path_ != nullptr) {
    const DocPath sub_path(range_doc_path_->encoded_doc_key(),
                           PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn));
    const auto value = Value(PrimitiveValue(), ttl, user_timestamp);
    RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
        sub_path, value, data.read_time, data.deadline, request_.stmt_id()));
  }

  for (const auto& column_value : request_.column_values()) {
    // Get the column.
    if (!column_value.has_column_id()) {
      return STATUS_FORMAT(InvalidArgument, "column id missing: $0",
                           column_value.DebugString());
    }
    const ColumnId column_id(column_value.column_id());
    auto column = schema_.column_by_id(column_id);
    RETURN_NOT_OK(column);

    // Check column-write operator.
    CHECK(GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert)
      << "Illegal write instruction";

    // Evaluate column value.
    QLValue expr_result;
    RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, &expr_result));
    const SubDocument sub_doc =
        SubDocument::FromQLValuePB(expr_result.value(), column->sorting_type());

    // Inserting into specified column.
    DocPath sub_path(range_doc_path_->encoded_doc_key(), PrimitiveValue(column_id));
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path, sub_doc, data.read_time, data.deadline, request_.stmt_id(), ttl, user_timestamp));
  }

  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

CHECKED_STATUS PgsqlWriteOperation::ApplyUpdate(const DocOperationApplyData& data) {
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

CHECKED_STATUS PgsqlWriteOperation::ApplyDelete(const DocOperationApplyData& data) {
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

CHECKED_STATUS PgsqlWriteOperation::ReadColumns(const DocOperationApplyData& data,
                                                const QLTableRow::SharedPtr& table_row) {
  // Create projections to scan docdb.
  Schema column_projection;
  RETURN_NOT_OK(CreateProjections(schema_, request_.column_refs(), &column_projection));

  // Filter the columns using primary key.
  if (range_doc_key_ != nullptr) {
    DocPgsqlScanSpec spec(column_projection, request_.stmt_id(), *range_doc_key_);
    DocRowwiseIterator iterator(column_projection,
                                schema_,
                                txn_op_context_,
                                data.doc_write_batch->doc_db(),
                                data.deadline,
                                data.read_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(table_row.get()));
    } else {
      table_row->Clear();
    }
    data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
  }

  return Status::OK();
}

void PgsqlWriteOperation::GetDocPathsToLock(list<DocPath> *paths, IsolationLevel *level) const {
  if (hashed_doc_path_ != nullptr) {
    paths->push_back(*hashed_doc_path_);
  }
  if (range_doc_path_ != nullptr) {
    paths->push_back(*range_doc_path_);
  }
  // When this write operation requires a read, it requires a read snapshot so paths will be locked
  // in snapshot isolation for consistency. Otherwise, pure writes will happen in serializable
  // isolation so that they will serialize but do not conflict with one another.
  //
  // Currently, only keys that are being written are locked, no lock is taken on read at the
  // snapshot isolation level.
  *level = RequireReadSnapshot() ? IsolationLevel::SNAPSHOT_ISOLATION
                                 : IsolationLevel::SERIALIZABLE_ISOLATION;
}

//--------------------------------------------------------------------------------------------------

Status PgsqlReadOperation::Execute(const common::YQLStorageIf& ql_storage,
                                   MonoTime deadline,
                                   const ReadHybridTime& read_time,
                                   const Schema& schema,
                                   const Schema& query_schema,
                                   PgsqlResultSet *resultset,
                                   HybridTime *restart_read_ht) {
  size_t row_count_limit = std::numeric_limits<std::size_t>::max();
  if (request_.has_limit()) {
    if (request_.limit() == 0) {
      return Status::OK();
    }
    row_count_limit = request_.limit();
  }

  // Create the projection of regular columns selected by the row block plus any referenced in
  // the WHERE condition. When DocRowwiseIterator::NextRow() populates the value map, it uses this
  // projection only to scan sub-documents. The query schema is used to select only referenced
  // columns and key columns.
  Schema projection;
  RETURN_NOT_OK(CreateProjections(schema, request_.column_refs(), &projection));

  // Construct scan specification and iterator.
  common::YQLRowwiseIteratorIf::UniPtr iter;
  common::PgsqlScanSpec::UniPtr spec;
  ReadHybridTime req_read_time;
  RETURN_NOT_OK(ql_storage.BuildYQLScanSpec(request_, read_time, schema, &spec, &req_read_time));
  RETURN_NOT_OK(ql_storage.GetIterator(request_, query_schema, schema, txn_op_context_,
                                       deadline, req_read_time, *spec, &iter));
  if (FLAGS_trace_docdb_calls) {
    TRACE("Initialized iterator");
  }

  // Fetching data.
  int match_count = 0;
  QLTableRow::SharedPtr selected_row = make_shared<QLTableRow>();
  while (resultset->rsrow_count() < row_count_limit && iter->HasNext()) {
    // The filtering process runs in the following order.
    // <hash_code><hash_components><range_components><regular_column_id> -> value;
    selected_row->Clear();
    RETURN_NOT_OK(iter->NextRow(projection, selected_row.get()));

    // Match the row with the where condition before adding to the row block.
    bool is_match = true;
    if (request_.has_where_expr()) {
      QLValue match;
      RETURN_NOT_OK(EvalExpr(request_.where_expr(), selected_row, &match));
      is_match = match.bool_value();
    }
    if (is_match) {
      match_count++;
      if (request_.is_aggregate()) {
        RETURN_NOT_OK(EvalAggregate(selected_row));
      } else {
        RETURN_NOT_OK(PopulateResultSet(selected_row, resultset));
      }
    }
  }

  if (request_.is_aggregate() && match_count > 0) {
    RETURN_NOT_OK(PopulateAggregate(selected_row, resultset));
  }

  if (FLAGS_trace_docdb_calls) {
    TRACE("Fetched $0 rows.", resultset->rsrow_count());
  }
  *restart_read_ht = iter->RestartReadHt();

  if (resultset->rsrow_count() >= row_count_limit && !request_.is_aggregate()) {
    RETURN_NOT_OK(iter->SetPagingStateIfNecessary(request_, &response_));
  }

  return Status::OK();
}

CHECKED_STATUS PgsqlReadOperation::PopulateResultSet(const QLTableRow::SharedPtr& table_row,
                                                     PgsqlResultSet *resultset) {
  int column_count = request_.targets().size();
  PgsqlRSRow *rsrow = resultset->AllocateRSRow(column_count);

  int rscol_index = 0;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    RETURN_NOT_OK(EvalExpr(expr, table_row, rsrow->rscol(rscol_index)));
    rscol_index++;
  }

  return Status::OK();
}

CHECKED_STATUS PgsqlReadOperation::EvalAggregate(const QLTableRow::SharedPtr& table_row) {
  if (aggr_result_.empty()) {
    int column_count = request_.targets().size();
    aggr_result_.resize(column_count);
  }

  int aggr_index = 0;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    RETURN_NOT_OK(EvalExpr(expr, table_row, &aggr_result_[aggr_index]));
    aggr_index++;
  }
  return Status::OK();
}

CHECKED_STATUS PgsqlReadOperation::PopulateAggregate(const QLTableRow::SharedPtr& table_row,
                                                     PgsqlResultSet *resultset) {
  int column_count = request_.targets().size();
  PgsqlRSRow *rsrow = resultset->AllocateRSRow(column_count);
  for (int rscol_index = 0; rscol_index < column_count; rscol_index++) {
    *rsrow->rscol(rscol_index) = aggr_result_[rscol_index];
  }
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
