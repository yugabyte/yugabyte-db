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

#include <numeric> // NOLINT - needed because header name mismatch source name

#include <boost/algorithm/string.hpp>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "yb/client/schema.h"

#include "yb/common/ql_value.h"

#include "yb/util/result.h"
#include "yb/util/string_util.h"

#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_insert.h"
#include "yb/yql/cql/ql/ptree/pt_insert_json_clause.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"

namespace yb {
namespace ql {

namespace {
// General error that should be shown when outermost JSON decoding fails
const std::string kJsonMapDecodeErrMsg("Could not decode JSON string as a map");

// Error message stating that the given JSON string could not be parsed as a given type
std::string GetInnerParseErr(const Slice& string_value,
                             const QLType::SharedPtr& dst_type) {
  return Format("Unable to make $0 from '$1'", *dst_type, string_value);
}

// Error message stating that types are not compatible
std::string GetCoercionErr(const DataType src_type,
                           const DataType dst_type) {
  return Format("Unable to make $0 from $1",
                QLType::ToCQLString(dst_type),
                QLType::ToCQLString(src_type));
}

// Parses JSON string as rapidjson document
Result<rapidjson::Document> ParseJsonString(const char* json_string,
                                            ExecContext* exec_context,
                                            const YBLocationPtr& loc) {
  // Special case: boolean strings in CQL are case-insensitive, but rapidjson disagrees
  if (strcasecmp(json_string, "true") == 0) {
    json_string = "true";
  } else if (strcasecmp(json_string, "false") == 0) {
    json_string = "false";
  }

  rapidjson::Document document;
  document.Parse<rapidjson::ParseFlag::kParseNumbersAsStringsFlag>(json_string);
  if (document.HasParseError()) {
    // TODO: Location offset to pinpoint an error?
    return exec_context->Error(*loc,
                               rapidjson::GetParseError_En(document.GetParseError()),
                               ErrorCode::INVALID_ARGUMENTS);
  }
  return document;
}
} // anonymous namespace

std::string NormalizeJsonKey(const std::string& key) {
  if (boost::starts_with(key, "\"") && boost::ends_with(key, "\"")) {
    return key.substr(1, key.size() - 2);
  } else {
    return boost::algorithm::to_lower_copy(key);
  }
}

Result<PTExpr::SharedPtr> Executor::ConvertJsonToExpr(const rapidjson::Value& json_value,
                                                      const QLType::SharedPtr& type,
                                                      const YBLocationPtr& loc) {
  CHECK_NOTNULL(type.get());

  // Strip FROZEN wrapping and process underlying type
  if (type->main() == DataType::FROZEN) {
    auto result = VERIFY_RESULT(ConvertJsonToExpr(json_value, type->param_type(0), loc));
    result->set_ql_type(type); // Execution expects explicit FROZEN type
    return result;
  }

  PTExpr::SharedPtr value_expr = VERIFY_RESULT(ConvertJsonToExprInner(json_value, type, loc));
  value_expr->set_expected_internal_type(client::YBColumnSchema::ToInternalDataType(type));

  if (!QLType::IsImplicitlyConvertible(type, value_expr->ql_type())) {
    return exec_context_->Error(value_expr,
                                GetCoercionErr(value_expr->ql_type_id(), type->main()),
                                ErrorCode::DATATYPE_MISMATCH);
  }
  return value_expr;
}

Result<PTExpr::SharedPtr> Executor::ConvertJsonToExprInner(const rapidjson::Value& json_value,
                                                           const QLType::SharedPtr& type,
                                                           const YBLocationPtr& loc) {
  MemoryContext* memctx = exec_context_->PTempMem();
  switch (json_value.GetType()) {
    case rapidjson::Type::kNullType: {
      return PTNull::MakeShared(memctx, loc, nullptr);
    }
    case rapidjson::Type::kTrueType: FALLTHROUGH_INTENDED;
    case rapidjson::Type::kFalseType: {
      return PTConstBool::MakeShared(memctx, loc, json_value.GetBool());
    }
    case rapidjson::Type::kStringType: {
      //
      // Process strings
      //
      // Things to keep in mind here:
      // 1) INSERT JSON allows string for every single type of value,
      // which requires additional layer of JSON parsing.
      // 2) We specifically instruct JSON parser to parse numerics as strings
      // to avoid precision loss and overflow.
      //
      const char* json_value_string = json_value.GetString();
      const auto mc_string = MCMakeShared<MCString>(memctx, json_value_string);
      if (QLType::IsImplicitlyConvertible(type->main(), DataType::STRING)) {
        return PTConstText::MakeShared(memctx, loc, mc_string);
      } else if (yb::IsBigInteger(json_value_string)) {
        return PTConstVarInt::MakeShared(memctx, loc, mc_string);
      } else if (yb::IsDecimal(json_value_string)) {
        return PTConstDecimal::MakeShared(memctx, loc, mc_string);
      } else {
        // Parse string as JSON
        auto json_expr_result = ParseJsonString(json_value_string, exec_context_, loc);
        if (json_expr_result.ok()) {
          return VERIFY_RESULT(ConvertJsonToExpr(*json_expr_result, type, loc));
        } else {
          return exec_context_->Error(*loc,
                                      GetInnerParseErr(json_value_string, type),
                                      ErrorCode::DATATYPE_MISMATCH);
        }
      }
    }
    case rapidjson::Type::kArrayType: {
      // All of these collections are represented as JSON lists
      // TODO: Add tuple during #936
      if (type->main() != DataType::LIST && type->main() != DataType::SET) {
        return exec_context_->Error(*loc,
                                    GetCoercionErr(DataType::LIST, type->main()),
                                    ErrorCode::DATATYPE_MISMATCH);
      }
      auto result = PTCollectionExpr::MakeShared(memctx, loc, type);
      for (const auto& member : json_value.GetArray()) {
        result->AddElement(VERIFY_RESULT(ConvertJsonToExpr(member, type->values_type(), loc)));
      }
      return result;
    }
    case rapidjson::Type::kObjectType: {
      // Could be either Map or UDT
      if (type->main() != DataType::MAP && type->main() != DataType::USER_DEFINED_TYPE) {
        return exec_context_->Error(*loc,
                                    GetCoercionErr(DataType::MAP, type->main()),
                                    ErrorCode::DATATYPE_MISMATCH);
      }
      auto result =  PTCollectionExpr::MakeShared(memctx, loc, type);
      for (const auto& member : json_value.GetObject()) {
        if (type->main() == DataType::MAP) {
          const PTExpr::SharedPtr processed_key =
              VERIFY_RESULT(ConvertJsonToExpr(member.name, type->keys_type(), loc));
          const PTExpr::SharedPtr processed_value =
              VERIFY_RESULT(ConvertJsonToExpr(member.value, type->values_type(), loc));
          result->AddKeyValuePair(processed_key, processed_value);
        } else { // UDT
          const auto key_string = NormalizeJsonKey(member.name.GetString());
          const auto value_type = VERIFY_RESULT(type->GetUDTFieldTypeByName(key_string));
          if (!value_type) {
            return exec_context_->Error(*loc,
                                        "Key '" + key_string + "' not found in user-defined type",
                                        ErrorCode::DATATYPE_MISMATCH);
          }
          const auto name_node =
              PTQualifiedName::MakeShared(memctx,
                                          loc,
                                          MCMakeShared<MCString>(memctx, key_string.c_str()));
          const PTExpr::SharedPtr processed_key =
              PTRef::MakeShared(memctx, loc, name_node);
          const PTExpr::SharedPtr processed_value =
              VERIFY_RESULT(ConvertJsonToExpr(member.value, value_type, loc));
          result->AddKeyValuePair(processed_key, processed_value);
        }
      }
      if (type->main() == DataType::USER_DEFINED_TYPE) {
        RETURN_NOT_OK(result->InitializeUDTValues(type, exec_context_));
      }
      return result;
    }
    case rapidjson::Type::kNumberType: {
      // We're using kParseNumbersAsStringsFlag flag, so this shouldn't be possible
      return exec_context_->Error(*loc,
                                  "Unexpected numeric type in JSON processing",
                                  ErrorCode::SERVER_ERROR);
    }
  }
  FATAL_INVALID_ENUM_VALUE(rapidjson::Type, json_value.GetType());
}

Status Executor::PreExecTreeNode(PTInsertJsonClause* json_clause) {

  //
  // Resolve JSON string
  //
  QLValuePB json_expr_pb;
  RETURN_NOT_OK(PTConstToPB(json_clause->Expr(), &json_expr_pb));
  const std::string& json_string = json_expr_pb.string_value();

  //
  // Parse JSON and store the result
  //
  auto json_document = VERIFY_RESULT(ParseJsonString(json_string.c_str(),
                                                     exec_context_,
                                                     json_clause->Expr()->loc_ptr()));
  if (json_document.GetType() != rapidjson::Type::kObjectType) {
    return exec_context_->Error(json_clause->Expr(),
                                kJsonMapDecodeErrMsg,
                                ErrorCode::INVALID_ARGUMENTS);
  }
  return json_clause->PreExecInit(json_string, std::move(json_document));
}

Status Executor::InsertJsonClauseToPB(const PTInsertStmt* insert_stmt,
                                      const PTInsertJsonClause* json_clause,
                                      QLWriteRequestPB* req) {
  const auto& column_map = insert_stmt->column_map();
  const auto& loc        = json_clause->Expr()->loc_ptr();

  // Processed columns with their associated QL expressions
  std::map<const ColumnDesc*, QLExpressionPB*> processed_cols;

  // Process all columns in JSON clause
  for (const auto& member : json_clause->JsonDocument().GetObject()) {
    const rapidjson::Value& key   = member.name;
    const rapidjson::Value& value = member.value;

    SCHECK(key.IsString(), InvalidArgument, "JSON root object key must be a string");
    const MCSharedPtr<MCString>& mc_col_name =
        MCMakeShared<MCString>(exec_context_->PTempMem(),
                               NormalizeJsonKey(key.GetString()).c_str());
    auto found_column_entry = column_map.find(*mc_col_name);
    // Check that the column exists.
    if (found_column_entry == column_map.end()) {
      return exec_context_->Error(*loc, mc_col_name->c_str(), ErrorCode::UNDEFINED_COLUMN);
    }

    const ColumnDesc& col_desc = found_column_entry->second;
    QLExpressionPB* expr_pb = processed_cols[&col_desc];
    if (!expr_pb) {
      expr_pb = CreateQLExpression(req, col_desc);
      processed_cols[&col_desc] = expr_pb;
    }
    QLValuePB* value_pb = expr_pb->mutable_value();
    PTExpr::SharedPtr expr = VERIFY_RESULT(ConvertJsonToExpr(value, col_desc.ql_type(), loc));
    RETURN_NOT_OK(PTConstToPB(expr, value_pb, false));
  }

  // Perform checks and init columns not mentioned in JSON
  for (auto iter = column_map.begin(); iter != column_map.end(); iter++) {
    const ColumnDesc& col_desc = iter->second;

    const auto& found_col_entry = processed_cols.find(&col_desc);
    bool not_found = found_col_entry == processed_cols.end();

    // Null values not allowed for primary key
    if (col_desc.is_primary()
        && (not_found
            || !found_col_entry->second->has_value()
            || IsNull(found_col_entry->second->value()))) {
      LOG(INFO) << "Unexpected null value. Current request: " << req->DebugString();
      return exec_context_->Error(*loc, ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
    }

    // All non-mentioned columns should be set to NULL
    if (not_found && json_clause->IsDefaultNull()) {
      QLExpressionPB* expr_pb = CreateQLExpression(req, col_desc);
      SetNull(expr_pb->mutable_value());
    }
  }

  return Status::OK();
}

}  // namespace ql
}  // namespace yb
