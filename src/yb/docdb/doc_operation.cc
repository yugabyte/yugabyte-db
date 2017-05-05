// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_scanspec.h"
#include "yb/common/yql_storage_interface.h"
#include "yb/common/yql_value.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_yql_scanspec.h"
#include "yb/docdb/subdocument.h"
#include "yb/server/hybrid_clock.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/common/partition.h"
#include "yb/util/trace.h"

DECLARE_bool(trace_docdb_calls);

using strings::Substitute;

namespace yb {
namespace docdb {

using std::set;
using std::list;
using strings::Substitute;

list<DocPath> KuduWriteOperation::DocPathsToLock() const {
  return { doc_path_ };
}

Status KuduWriteOperation::Apply(
    DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) {
  return doc_write_batch->SetPrimitive(doc_path_, value_, InitMarkerBehavior::kOptional);
}

list<DocPath> RedisWriteOperation::DocPathsToLock() const {
  return {
    DocPath::DocPathFromRedisKey(request_.key_value().hash_code(), request_.key_value().key()) };
}

Status GetRedisValue(
    rocksdb::DB *rocksdb,
    HybridTime hybrid_time,
    const RedisKeyValuePB &key_value_pb,
    RedisDataType *type,
    string *value) {
  if (!key_value_pb.has_key()) {
    return STATUS(Corruption, "Expected KeyValuePB");
  }
  SubDocKey doc_key(DocKey::FromRedisKey(key_value_pb.hash_code(), key_value_pb.key()));

  if (!key_value_pb.subkey().empty()) {
    if (key_value_pb.subkey().size() != 1) {
      return STATUS_SUBSTITUTE(Corruption,
          "Expected at most one subkey, got $0", key_value_pb.subkey().size());
    }
    doc_key.AppendSubKeysAndMaybeHybridTime(PrimitiveValue(key_value_pb.subkey(0)));
  }

  SubDocument doc;
  bool doc_found = false;

  RETURN_NOT_OK(GetSubDocument(rocksdb, doc_key, &doc, &doc_found, hybrid_time));

  if (!doc_found) {
    *type = REDIS_TYPE_NONE;
    return Status::OK();
  }

  if (!doc.IsPrimitive()) {
    *type = REDIS_TYPE_HASH;
    return Status::OK();
  }

  *type = REDIS_TYPE_STRING;
  *value = doc.GetString();
  return Status::OK();
}

// Set response based on the type match. Return whether type is unexpected.
bool IsWrongType(
    const RedisDataType expected_type,
    const RedisDataType actual_type,
    RedisResponsePB *const response) {
  if (actual_type == RedisDataType::REDIS_TYPE_NONE) {
    response->set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
    return true;
  }
  if (actual_type != expected_type) {
    response->set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    return true;
  }
  response->set_code(RedisResponsePB_RedisStatusCode_OK);
  return false;
}

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
  CHECK_EQ(kv.value().size(), 1)
      << "Set operations are expected have exactly one value, found " << kv.value().size();
  const MonoDelta ttl = request_.set_request().has_ttl() ?
      MonoDelta::FromMilliseconds(request_.set_request().ttl()) : Value::kMaxTtl;
  RETURN_NOT_OK(
      doc_write_batch->SetPrimitive(
          DocPath::DocPathFromRedisKey(
              kv.hash_code(), kv.key(), kv.subkey_size() > 0 ? kv.subkey(0) : ""),
          Value(PrimitiveValue(kv.value(0)), ttl)));
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

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
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

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
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
  RETURN_NOT_OK(doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(ValueType::kTombstone))));
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  // Currently we only support deleting one key
  response_.set_int_response(1);
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

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  // TODO (akashnil): Handle overflows.
  value.replace(request_.set_range_request().offset(), kv.value(0).length(), kv.value(0));

  return doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.hash_code(), kv.key()),
      Value(PrimitiveValue(value)));
}

Status RedisWriteOperation::ApplyIncr(DocWriteBatch* doc_write_batch, int64_t incr) {
  RedisDataType type;
  string value;
  const RedisKeyValuePB& kv = request_.key_value();

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_hybrid_time_, kv, &type, &value));

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
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
  return STATUS(NotSupported, "Redis operation has not been implemented");
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

Status RedisReadOperation::ExecuteGet(rocksdb::DB *rocksdb, HybridTime hybrid_time) {

  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  response_.set_string_response(value);
  return Status::OK();
}

Status RedisReadOperation::ExecuteStrLen(rocksdb::DB *rocksdb, HybridTime hybrid_time) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  response_.set_int_response(value.length());
  return Status::OK();
}

Status RedisReadOperation::ExecuteExists(rocksdb::DB *rocksdb, HybridTime hybrid_time) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));

  if (type == REDIS_TYPE_STRING || type == REDIS_TYPE_HASH) {
    response_.set_code(RedisResponsePB_RedisStatusCode_OK);
    // We only support exist command with one argument currently.
    response_.set_int_response(1);
  } else if (type == REDIS_TYPE_NONE) {
    response_.set_code(RedisResponsePB_RedisStatusCode_OK);
    response_.set_int_response(0);
  } else {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
  }
  return Status::OK();
}

Status RedisReadOperation::ExecuteGetRange(rocksdb::DB *rocksdb, HybridTime hybrid_time) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, hybrid_time, request_.key_value(), &type, &value));

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
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

  response_.set_string_response(value.substr(start, end - start));
  return Status::OK();
}

const RedisResponsePB& RedisReadOperation::response() {
  return response_;
}

namespace {

CHECKED_STATUS GetNonKeyColumns(
    const Schema& schema, const google::protobuf::RepeatedPtrField<yb::YQLExpressionPB>& operands,
    set<ColumnId>* static_columns, set<ColumnId>* non_static_columns);

// Get all non-key columns referenced in a condition.
CHECKED_STATUS GetNonKeyColumns(
    const Schema& schema, const YQLConditionPB& condition,
    set<ColumnId>* static_columns, set<ColumnId>* non_static_columns) {
  return GetNonKeyColumns(schema, condition.operands(), static_columns, non_static_columns);
}

// Get all non-key columns referenced in a list of operands.
CHECKED_STATUS GetNonKeyColumns(
    const Schema& schema, const google::protobuf::RepeatedPtrField<yb::YQLExpressionPB>& operands,
    set<ColumnId>* static_columns, set<ColumnId>* non_static_columns) {
  for (const auto& operand : operands) {
    switch (operand.expr_case()) {
      case YQLExpressionPB::ExprCase::kValue:
        continue;
      case YQLExpressionPB::ExprCase::kColumnId: {
        const auto column_id = ColumnId(operand.column_id());
        if (!schema.is_key_column(column_id)) {
          if (schema.column_by_id(column_id).is_static()) {
            static_columns->insert(column_id);
          } else {
            non_static_columns->insert(column_id);
          }
        }
        continue;
      }
      case YQLExpressionPB::ExprCase::kCondition:
        RETURN_NOT_OK(
            GetNonKeyColumns(schema, operand.condition(), static_columns, non_static_columns));
        continue;
      case YQLExpressionPB::ExprCase::EXPR_NOT_SET:
        return STATUS(Corruption, "expression not set");
    }
    return STATUS(
        RuntimeError, Substitute("Expression type $0 not supported", operand.expr_case()));
  }
  return Status::OK();
}

// Create projection schemas of static and non-static columns from a rowblock projection schema
// (for read) and a WHERE / IF condition (for read / write). "schema" is the full table schema
// and "rowblock_schema" is the selected columns from which we are splitting into static and
// non-static column portions.
CHECKED_STATUS CreateProjections(
    const Schema& schema, const Schema* rowblock_schema, const YQLConditionPB* condition,
    Schema* static_projection, Schema* non_static_projection) {
  // The projection schemas are used to scan docdb. Keep the columns to fetch in sorted order for
  // more efficient scan in the iterator.
  set<ColumnId> static_columns, non_static_columns;

  // Note that we use "schema" instead of "rowblock_schema" to fetch column metadata below. This
  // is necessary because rowblock_schema is not populated with column ids. Also when we support
  // expressions in select-list, those expression columns do not have column ids.
  if (rowblock_schema != nullptr) {
    for (size_t idx = 0; idx < rowblock_schema->num_columns(); idx++) {
      const auto column_id = rowblock_schema->column_id(idx);
      if (!schema.is_key_column(column_id)) {
        if (schema.column_by_id(column_id).is_static()) {
          static_columns.insert(column_id);
        } else {
          non_static_columns.insert(column_id);
        }
      }
    }
  }

  if (condition != nullptr) {
    RETURN_NOT_OK(GetNonKeyColumns(schema, *condition, &static_columns, &non_static_columns));
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
    const YQLValueMap& value_map, const Schema& projection, size_t col_idx, YQLRow* row) {
  for (size_t i = 0; i < projection.num_columns(); i++) {
    const auto column_id = projection.column_id(i);
    const auto it = value_map.find(column_id);
    if (it != value_map.end()) {
      *row->mutable_column(col_idx) = std::move(it->second);
    }
    col_idx++;
  }
}

// Join a static row with a non-static row.
void JoinStaticRow(
    const Schema& schema, const Schema& static_projection, const YQLValueMap& static_row,
    YQLValueMap* non_static_row) {
  // No need to join if static row is empty or the hash key is different.
  if (static_row.empty()) {
    return;
  }
  for (size_t i = 0; i < schema.num_hash_key_columns(); i++) {
    const ColumnId column_id = schema.column_id(i);
    if (static_row.at(column_id) != non_static_row->at(column_id)) {
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

YQLWriteOperation::YQLWriteOperation(
    const YQLWriteRequestPB& request, const Schema& schema, YQLResponsePB* response)
    : schema_(schema), request_(request), response_(response) {
  // Determine if static / non-static columns are being written.
  bool write_static_columns = false;
  bool write_non_static_columns = false;
  for (const auto& column : request.column_values()) {
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
      !request.range_column_values().empty() ||
      schema.num_range_key_columns() == 0));
}

Status YQLWriteOperation::InitializeKeys(const bool hashed_key, const bool primary_key) {
  // Populate the hashed and range components in the same order as they are in the table schema.
  const auto& hashed_column_values = request_.hashed_column_values();
  const auto& range_column_values = request_.range_column_values();
  vector<PrimitiveValue> hashed_components;
  vector<PrimitiveValue> range_components;
  RETURN_NOT_OK(YQLColumnValuesToPrimitiveValues(
      hashed_column_values, schema_, 0,
      schema_.num_hash_key_columns(), &hashed_components));
  RETURN_NOT_OK(YQLColumnValuesToPrimitiveValues(
      range_column_values, schema_, schema_.num_hash_key_columns(),
      schema_.num_key_columns() - schema_.num_hash_key_columns(), &range_components));

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

bool YQLWriteOperation::RequireReadSnapshot() const {
  return request_.has_if_condition();
}

list<DocPath> YQLWriteOperation::DocPathsToLock() const {
  list<DocPath> paths;
  if (hashed_doc_path_ != nullptr)
    paths.push_back(*hashed_doc_path_);
  if (pk_doc_path_ != nullptr)
    paths.push_back(*pk_doc_path_);
  return paths;
}

Status YQLWriteOperation::IsConditionSatisfied(
    const YQLConditionPB& condition, rocksdb::DB *rocksdb, const HybridTime& hybrid_time,
    bool* should_apply, std::unique_ptr<YQLRowBlock>* rowblock) {

  // Create projections to scan docdb.
  Schema static_projection, non_static_projection;
  RETURN_NOT_OK(CreateProjections(
      schema_, nullptr /* rowblock_schema */, &condition,
      &static_projection, &non_static_projection));

  // Generate hashed / primary key depending on if static / non-static columns are referenced in
  // the if-condition.
  RETURN_NOT_OK(InitializeKeys(
      !static_projection.columns().empty(), !non_static_projection.columns().empty()));

  // Scan docdb for the static and non-static columns of the row using the hashed / primary key.
  YQLValueMap value_map;
  if (hashed_doc_key_ != nullptr) {
    DocYQLScanSpec spec(static_projection, *hashed_doc_key_);
    DocRowwiseIterator iterator(static_projection, schema_, rocksdb, hybrid_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(static_projection, &value_map));
    }
  }
  if (pk_doc_key_ != nullptr) {
    DocYQLScanSpec spec(non_static_projection, *pk_doc_key_);
    DocRowwiseIterator iterator(non_static_projection, schema_, rocksdb, hybrid_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (iterator.HasNext()) {
      RETURN_NOT_OK(iterator.NextRow(non_static_projection, &value_map));
    } else {
      // If no non-static column is found, the row does not exist and we should clear the static
      // columns in the map to indicate the row does not exist.
      value_map.clear();
    }
  }

  // See if the if-condition is satisfied.
  RETURN_NOT_OK(EvaluateCondition(condition, value_map, should_apply));

  // Populate the result set to return the "applied" status, and optionally the present column
  // values if the condition is not satisfied and the row does exist (value_map is not empty).
  vector<ColumnSchema> columns;
  columns.emplace_back(ColumnSchema("[applied]", BOOL));
  if (!*should_apply && !value_map.empty()) {
    columns.insert(columns.end(),
                   static_projection.columns().begin(), static_projection.columns().end());
    columns.insert(columns.end(),
                   non_static_projection.columns().begin(), non_static_projection.columns().end());
  }
  rowblock->reset(new YQLRowBlock(Schema(columns, 0)));
  YQLRow& row = rowblock->get()->Extend();
  row.mutable_column(0)->set_bool_value(*should_apply);
  if (!*should_apply && !value_map.empty()) {
    PopulateRow(value_map, static_projection, 1 /* begin col_idx */, &row);
    PopulateRow(value_map, non_static_projection, 1 + static_projection.num_columns(), &row);
  }

  return Status::OK();
}

Status YQLWriteOperation::Apply(
    DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const HybridTime& hybrid_time) {

  bool should_apply = true;
  if (request_.has_if_condition()) {
    RETURN_NOT_OK(IsConditionSatisfied(
        request_.if_condition(), rocksdb, hybrid_time, &should_apply, &rowblock_));
  }

  if (should_apply) {
    const MonoDelta ttl =
        request_.has_ttl() ? MonoDelta::FromMilliseconds(request_.ttl()) : Value::kMaxTtl;

    switch (request_.type()) {
      // YQL insert == update (upsert) to be consistent with Cassandra's semantics. In either
      // INSERT or UPDATE, if non-key columns are specified, they will be inserted which will cause
      // the primary key to be inserted also when necessary. Otherwise, we should insert the
      // primary key at least.
      case YQLWriteRequestPB::YQL_STMT_INSERT:
      case YQLWriteRequestPB::YQL_STMT_UPDATE: {
        // Add the appropriate liveness column only for inserts.
        // We never use init markers for YQL to ensure we perform writes without any reads to
        // ensure our write path is fast while complicating the read path a bit.
        if (request_.type() == YQLWriteRequestPB::YQL_STMT_INSERT && pk_doc_path_ != nullptr) {
          const DocPath sub_path(pk_doc_path_->encoded_doc_key(),
                                 PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn));
          const auto value = Value(PrimitiveValue(), ttl);
          RETURN_NOT_OK(doc_write_batch->SetPrimitive(sub_path, value,
              InitMarkerBehavior::kOptional));
        }
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
            const auto sub_doc = SubDocument::FromYQLValuePB(column.type(), column_value.value(),
                                                             column.sorting_type());
            RETURN_NOT_OK(doc_write_batch->InsertSubDocument(
                sub_path, sub_doc, InitMarkerBehavior::kOptional, ttl));
          }
        }
        break;
      }
      case YQLWriteRequestPB::YQL_STMT_DELETE: {
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
            RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(sub_path, InitMarkerBehavior::kOptional));
          }
        } else {
          RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(
              *pk_doc_path_, InitMarkerBehavior::kOptional));
        }
        break;
      }
    }

    // In all cases, something should be written so the write batch shouldn't be empty.
    CHECK(!doc_write_batch->IsEmpty()) << "Empty write batch " << request_.type();
  }

  response_->set_status(YQLResponsePB::YQL_STATUS_OK);

  return Status::OK();
}

YQLReadOperation::YQLReadOperation(const YQLReadRequestPB& request) : request_(request) {
}

Status YQLReadOperation::Execute(
    const common::YQLStorageIf& yql_storage, const HybridTime& hybrid_time, const Schema& schema,
    YQLRowBlock* rowblock) {

  size_t row_count_limit = std::numeric_limits<std::size_t>::max();
  if (request_.has_limit()) {
    if (request_.limit() == 0) {
      return Status::OK();
    }
    row_count_limit = request_.limit();
  }

  // Create the projections of the non-key columns selected by the row block plus any referenced in
  // the WHERE condition. When DocRowwiseIterator::NextRow() populates the value map, it uses this
  // projection only to scan sub-documents. YQLRowBlock's own projection schema is used to populate
  // the row block, including key columns if any.
  Schema static_projection, non_static_projection;
  RETURN_NOT_OK(CreateProjections(
      schema, &rowblock->schema(),
      request_.has_where_condition() ? &request_.where_condition() : nullptr,
      &static_projection, &non_static_projection));
  const bool read_static_columns = !static_projection.columns().empty();
  const bool read_distinct_columns = request_.distinct();

  std::unique_ptr<common::YQLRowwiseIteratorIf> iter;
  std::unique_ptr<common::YQLScanSpec> spec;
  HybridTime req_hybrid_time;
  RETURN_NOT_OK(yql_storage.BuildYQLScanSpec(request_, hybrid_time, schema, read_static_columns,
                                             &spec, &req_hybrid_time));
  RETURN_NOT_OK(yql_storage.GetIterator(rowblock->schema(), schema, req_hybrid_time, &iter));
  RETURN_NOT_OK(iter->Init(*spec));
  if (FLAGS_trace_docdb_calls) {
    TRACE("Initialized iterator");
  }
  YQLValueMap static_row, non_static_row;
  YQLValueMap& selected_row = read_distinct_columns ? static_row : non_static_row;

  while (rowblock->row_count() < row_count_limit && iter->HasNext()) {

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
      YQLRow& row = rowblock->Extend();
      PopulateRow(selected_row, row.schema(), 0 /* begin col_idx */, &row);
    }
  }
  if (FLAGS_trace_docdb_calls) {
    TRACE("Fetched $0 rows.", rowblock->row_count());
  }

  RETURN_NOT_OK(iter->SetPagingStateIfNecessary(request_, *rowblock, row_count_limit, &response_));

  return Status::OK();
}

const YQLResponsePB& YQLReadOperation::response() const { return response_; }

}  // namespace docdb
}  // namespace yb
