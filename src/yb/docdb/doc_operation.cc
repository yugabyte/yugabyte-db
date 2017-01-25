// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_operation.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/ysql_scanspec.h"
#include "yb/docdb/subdocument.h"
#include "yb/server/hybrid_clock.h"
#include "yb/gutil/strings/substitute.h"

using strings::Substitute;

namespace yb {
namespace docdb {

using strings::Substitute;

DocPath KuduWriteOperation::DocPathToLock() const {
  return doc_path_;
}

Status KuduWriteOperation::Apply(
    DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const Timestamp& timestamp) {
  return doc_write_batch->SetPrimitive(doc_path_, value_, Timestamp::kMax);
}

DocPath RedisWriteOperation::DocPathToLock() const {
  return DocPath::DocPathFromRedisKey(request_.key_value().key());
}

Status GetRedisValue(
    rocksdb::DB *rocksdb,
    Timestamp timestamp,
    const RedisKeyValuePB &key_value_pb,
    RedisDataType *type,
    string *value) {
  if (!key_value_pb.has_key()) {
    return STATUS(Corruption, "Expected KeyValuePB");
  }
  KeyBytes doc_key = DocKey::FromRedisStringKey(key_value_pb.key()).Encode();

  if (!key_value_pb.subkey().empty()) {
    if (key_value_pb.subkey().size() != 1) {
      return STATUS_SUBSTITUTE(Corruption,
          "Expected at most one subkey, got $0", key_value_pb.subkey().size());
    }
    doc_key.AppendValueType(ValueType::kString);
    doc_key.AppendString(key_value_pb.subkey(0));
  }

  SubDocument doc;
  bool doc_found = false;

  RETURN_NOT_OK(GetSubDocument(rocksdb, doc_key, &doc, &doc_found, timestamp));

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
    DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const Timestamp& timestamp) {
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
          DocPath::DocPathFromRedisKey(kv.key(), kv.subkey_size() > 0 ? kv.subkey(0) : ""),
          Value(PrimitiveValue(kv.value(0)), ttl), Timestamp::kMax));
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

Status RedisWriteOperation::ApplyGetSet(DocWriteBatch* doc_write_batch) {
  RedisDataType type;
  string value;
  const RedisKeyValuePB& kv = request_.key_value();

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_timestamp_, kv, &type, &value));

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
      DocPath::DocPathFromRedisKey(kv.key()),
      Value(PrimitiveValue(kv.value(0))), Timestamp::kMax);
}

Status RedisWriteOperation::ApplyAppend(DocWriteBatch* doc_write_batch) {
  RedisDataType type;
  string value;
  const RedisKeyValuePB& kv = request_.key_value();

  if (kv.value_size() != 1) {
    return STATUS_SUBSTITUTE(Corruption,
        "Append kv should have 1 value, found $0", kv.value_size());
  }

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_timestamp_, kv, &type, &value));

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  const string& new_value = value + kv.value(0);

  response_.set_int_response(new_value.length());

  return doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.key()),
      Value(PrimitiveValue(new_value)), Timestamp::kMax);
}

// TODO (akashnil): Actually check if the value existed, return 0 if not. handle multidel in future.
//                  See ENG-807
Status RedisWriteOperation::ApplyDel(DocWriteBatch* doc_write_batch) {
  const RedisKeyValuePB& kv = request_.key_value();
  RETURN_NOT_OK(doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.key()),
      Value(PrimitiveValue(ValueType::kTombstone)), Timestamp::kMax));
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

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_timestamp_, kv, &type, &value));

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  // TODO (akashnil): Handle overflows.
  value.replace(request_.set_range_request().offset(), kv.value(0).length(), kv.value(0));

  return doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.key()),
      Value(PrimitiveValue(value)), Timestamp::kMax);
}

Status RedisWriteOperation::ApplyIncr(DocWriteBatch* doc_write_batch, int64_t incr) {
  RedisDataType type;
  string value;
  const RedisKeyValuePB& kv = request_.key_value();

  RETURN_NOT_OK(GetRedisValue(doc_write_batch->rocksdb(), read_timestamp_, kv, &type, &value));

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
      DocPath::DocPathFromRedisKey(kv.key()),
      Value(PrimitiveValue(std::to_string(new_value))), Timestamp::kMax);
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

Status RedisReadOperation::Execute(rocksdb::DB *rocksdb, const Timestamp& timestamp) {
  switch (request_.request_case()) {
    case RedisReadRequestPB::RequestCase::kGetRequest:
      return ExecuteGet(rocksdb, timestamp);
    case RedisReadRequestPB::RequestCase::kStrlenRequest:
      return ExecuteStrLen(rocksdb, timestamp);
    case RedisReadRequestPB::RequestCase::kExistsRequest:
      return ExecuteExists(rocksdb, timestamp);
    case RedisReadRequestPB::RequestCase::kGetRangeRequest:
      return ExecuteGetRange(rocksdb, timestamp);
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

Status RedisReadOperation::ExecuteGet(rocksdb::DB *rocksdb, Timestamp timestamp) {

  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, timestamp, request_.key_value(), &type, &value));

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  response_.set_string_response(value);
  return Status::OK();
}

Status RedisReadOperation::ExecuteStrLen(rocksdb::DB *rocksdb, Timestamp timestamp) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, timestamp, request_.key_value(), &type, &value));

  if (IsWrongType(RedisDataType::REDIS_TYPE_STRING, type, &response_)) {
    // We've already set the error code in the response.
    return Status::OK();
  }

  response_.set_int_response(value.length());
  return Status::OK();
}

Status RedisReadOperation::ExecuteExists(rocksdb::DB *rocksdb, Timestamp timestamp) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, timestamp, request_.key_value(), &type, &value));

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



Status RedisReadOperation::ExecuteGetRange(rocksdb::DB *rocksdb, Timestamp timestamp) {
  RedisDataType type;
  string value;

  RETURN_NOT_OK(GetRedisValue(rocksdb, timestamp, request_.key_value(), &type, &value));

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

// Add primary key column values to the component group. Verify that they are in the same order
// as in the table schema.
void YSQLColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& column_values,
    const Schema& schema, size_t column_idx, const size_t column_count,
    vector<PrimitiveValue>* components) {
  CHECK_EQ(column_values.size(), column_count) << "Primary key column count mismatch";
  for (const auto& column_value : column_values) {
    CHECK_EQ(schema.column_id(column_idx), column_value.column_id())
        << "Primary key column id mismatch";
    components->push_back(PrimitiveValue::FromYSQLValuePB(column_value.value()));
    column_idx++;
  }
}

// Populate dockey from YSQL key columns.
DocKey DocKeyFromYSQLKey(
    const Schema& schema,
    uint32_t hash_code,
    const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& hashed_column_values,
    const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& range_column_values) {
  vector<PrimitiveValue> hashed_components;
  vector<PrimitiveValue> range_components;

  // Populate the hashed and range components in the same order as they are in the table schema.
  YSQLColumnValuesToPrimitiveValues(
      hashed_column_values, schema, 0,
      schema.num_hash_key_columns(), &hashed_components);
  YSQLColumnValuesToPrimitiveValues(
      range_column_values, schema, schema.num_hash_key_columns(),
      schema.num_key_columns() - schema.num_hash_key_columns(), &range_components);

  return DocKey(hash_code, hashed_components, range_components);
}

CHECKED_STATUS GetNonKeyColumns(
    const Schema& schema, const google::protobuf::RepeatedPtrField<yb::YSQLExpressionPB>& operands,
    unordered_set<ColumnId>* non_key_columns);

// Get all non-key columns referenced in a condition.
CHECKED_STATUS GetNonKeyColumns(
    const Schema& schema, const YSQLConditionPB& condition,
    unordered_set<ColumnId>* non_key_columns) {
  return GetNonKeyColumns(schema, condition.operands(), non_key_columns);
}

// Get all non-key columns referenced in a list of operands.
CHECKED_STATUS GetNonKeyColumns(
    const Schema& schema, const google::protobuf::RepeatedPtrField<yb::YSQLExpressionPB>& operands,
    unordered_set<ColumnId>* non_key_columns) {
  for (const auto& operand : operands) {
    switch (operand.expr_case()) {
      case YSQLExpressionPB::ExprCase::kValue:
        continue;
      case YSQLExpressionPB::ExprCase::kColumnId: {
        const auto id = ColumnId(operand.column_id());
        const size_t idx = schema.find_column_by_id(id);
        if (!schema.is_key_column(idx)) {
          non_key_columns->insert(id);
        }
        continue;
      }
      case YSQLExpressionPB::ExprCase::kCondition:
        RETURN_NOT_OK(GetNonKeyColumns(schema, operand.condition(), non_key_columns));
        continue;
      case YSQLExpressionPB::ExprCase::EXPR_NOT_SET:
        return STATUS(Corruption, "expression not set");
    }
    return STATUS(
        RuntimeError, Substitute("Expression type $0 not supported", operand.expr_case()));
  }
  return Status::OK();
}

} // namespace

YSQLWriteOperation::YSQLWriteOperation(
    const YSQLWriteRequestPB& request, const Schema& schema, YSQLResponsePB* response)
    : schema_(schema),
      doc_key_(
          DocKeyFromYSQLKey(
              schema,
              request.hash_code(),
              request.hashed_column_values(),
              request.range_column_values())),
      doc_path_(DocPath(doc_key_.Encode())),
      request_(request),
      response_(response) {
}

bool YSQLWriteOperation::RequireReadSnapshot() const {
  return request_.has_if_condition();
}

DocPath YSQLWriteOperation::DocPathToLock() const {
  return doc_path_;
}

Status YSQLWriteOperation::IsConditionSatisfied(
    const YSQLConditionPB& condition, rocksdb::DB *rocksdb, const Timestamp& timestamp,
    bool* should_apply, std::unique_ptr<YSQLRowBlock>* rowblock) {
  // Prepare the projection schema to scan the docdb for the row.
  YSQLScanSpec spec(doc_key_);
  unordered_set<ColumnId> non_key_columns;
  RETURN_NOT_OK(GetNonKeyColumns(schema_, condition, &non_key_columns));
  Schema projection;
  RETURN_NOT_OK(
      schema_.CreateProjectionByIdsIgnoreMissing(
          vector<ColumnId>(non_key_columns.begin(), non_key_columns.end()), &projection));

  // Scan docdb for the row.
  YSQLValueMap value_map;
  DocRowwiseIterator iterator(projection, schema_, rocksdb, timestamp);
  RETURN_NOT_OK(iterator.Init(spec));
  if (iterator.HasNext()) {
    RETURN_NOT_OK(iterator.NextRow(spec, &value_map));
  }

  // See if the if-condition is satisfied.
  RETURN_NOT_OK(EvaluateCondition(condition, value_map, should_apply));

  // Populate the result set to return the "applied" status, and optionally the present column
  // values if the condition is not satisfied and the row does exist (value_map is not empty).
  vector<ColumnSchema> columns;
  columns.emplace_back(ColumnSchema("[applied]", BOOL));
  if (!*should_apply && !value_map.empty()) {
    columns.insert(columns.end(), projection.columns().begin(), projection.columns().end());
  }
  rowblock->reset(new YSQLRowBlock(Schema(columns, 0)));
  YSQLRow& row = rowblock->get()->Extend();
  row.set_bool_value(0, *should_apply);
  if (!*should_apply && !value_map.empty()) {
    for (size_t i = 0; i < projection.num_columns(); i++) {
      const auto column_id = projection.column_id(i);
      const auto it = value_map.find(column_id);
      CHECK(it != value_map.end()) << "Projected column missing: " << column_id;
      row.set_column(i + 1, std::move(it->second));
    }
  }

  return Status::OK();
}

Status YSQLWriteOperation::Apply(
    DocWriteBatch* doc_write_batch, rocksdb::DB *rocksdb, const Timestamp& timestamp) {

  bool should_apply = true;
  if (request_.has_if_condition()) {
    RETURN_NOT_OK(IsConditionSatisfied(
        request_.if_condition(), rocksdb, timestamp, &should_apply, &rowblock_));
  }

  if (should_apply) {
    const MonoDelta ttl =
        request_.has_ttl() ? MonoDelta::FromMilliseconds(request_.ttl()) : Value::kMaxTtl;

    switch (request_.type()) {
      // YSQL insert == update (upsert) to be consistent with Cassandra's semantics. In either
      // INSERT or UPDATE, if non-key columns are specified, they will be inserted which will cause
      // the primary key to be inserted also when necessary. Otherwise, we should insert the
      // primary key at least.
      case YSQLWriteRequestPB::YSQL_STMT_INSERT:
      case YSQLWriteRequestPB::YSQL_STMT_UPDATE: {
        if (request_.column_values_size() > 0) {
          for (const auto& column_value : request_.column_values()) {
            CHECK(column_value.has_column_id())
                << "column id missing: " << column_value.DebugString();
            CHECK(column_value.has_value())
                << "column value missing: " << column_value.DebugString();;
            const DocPath sub_path(doc_key_.Encode(), PrimitiveValue(column_value.column_id()));
            const auto value = Value(PrimitiveValue::FromYSQLValuePB(column_value.value()), ttl);
            RETURN_NOT_OK(doc_write_batch->SetPrimitive(sub_path, value));
          }
        } else {
          const auto value = Value(PrimitiveValue(ValueType::kObject), ttl);
          RETURN_NOT_OK(doc_write_batch->SetPrimitive(doc_path_, value));
        }
        break;
      }
      case YSQLWriteRequestPB::YSQL_STMT_DELETE: {
        // If non-key columns are specified, just the individual columns will be deleted. Otherwise,
        // the whole row is deleted.
        if (request_.column_values_size() > 0) {
          for (const auto& column_value : request_.column_values()) {
            CHECK(column_value.has_column_id())
                << "column id missing: " << column_value.DebugString();
            const DocPath sub_path(doc_key_.Encode(), PrimitiveValue(column_value.column_id()));
            RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(sub_path));
          }
        } else {
          RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(doc_path_));
        }
        break;
      }
    }

    // In all cases, something should be written so the write batch shouldn't be empty.
    CHECK(!doc_write_batch->IsEmpty()) << "Empty write batch " << request_.type();
  }

  response_->set_status(YSQLResponsePB::YSQL_STATUS_OK);

  return Status::OK();
}

YSQLReadOperation::YSQLReadOperation(const YSQLReadRequestPB& request) : request_(request) {
}

Status YSQLReadOperation::Execute(
    rocksdb::DB *rocksdb, const Timestamp& timestamp, const Schema& schema,
    YSQLRowBlock* rowblock) {

  if (request_.has_limit() && request_.limit() == 0) {
    return Status::OK();
  }

  // Populate dockey from YSQL key columns.
  docdb::DocKeyHash hash_code = request_.hash_code();
  vector<PrimitiveValue> hashed_components;
  YSQLColumnValuesToPrimitiveValues(
      request_.hashed_column_values(), schema, 0,
      schema.num_hash_key_columns(), &hashed_components);

  // Construct the scan spec basing on the WHERE condition.
  YSQLScanSpec spec(
      schema, hash_code, hashed_components,
      request_.has_where_condition() ? &request_.where_condition() : nullptr,
      request_.has_limit() ? request_.limit() : std::numeric_limits<std::size_t>::max());

  // Find the non-key columns selected by the row block plus any referenced in the WHERE condition.
  // When DocRowwiseIterator::NextBlock() populates a YSQLRowBlock, it uses this projection only to
  // scan sub-documents. YSQLRowBlock's own projection schema is used to pupulate the row block,
  // including key columns if any.
  unordered_set<ColumnId> non_key_columns;
  for (size_t idx = 0; idx < rowblock->schema().num_columns(); idx++) {
    const auto column_id = rowblock->schema().column_id(idx);
    if (!schema.is_key_column(schema.find_column_by_id(column_id))) {
      non_key_columns.insert(column_id);
    }
  }
  if (request_.has_where_condition()) {
    RETURN_NOT_OK(GetNonKeyColumns(schema, request_.where_condition(), &non_key_columns));
  }

  Schema projection;
  RETURN_NOT_OK(
      schema.CreateProjectionByIdsIgnoreMissing(
          vector<ColumnId>(non_key_columns.begin(), non_key_columns.end()), &projection));

  // Scan docdb for the rows.
  DocRowwiseIterator iterator(projection, schema, rocksdb, timestamp);
  RETURN_NOT_OK(iterator.Init(spec));
  while (iterator.HasNext()) {
    RETURN_NOT_OK(iterator.NextBlock(spec, rowblock));
  }

  return Status::OK();
}

const YSQLResponsePB& YSQLReadOperation::response() const { return response_; }

}  // namespace docdb
}  // namespace yb
