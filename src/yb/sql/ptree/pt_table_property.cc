// Copyright (c) YugaByte, Inc.

#include "yb/sql/ptree/pt_table_property.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

// These property names need to be lowercase, since we they are all converted to lowercase after
// the parser/scanner phase and as a result if we're doing string matching everything should be
// lowercase.
const char PTTableProperty::kDefaultTimeToLive[] = "default_time_to_live";
const std::map<std::string, client::YBColumnSchema::DataType> PTTableProperty::kPropertyDataTypes
    = {
    {PTTableProperty::kDefaultTimeToLive, client::YBColumnSchema::INT64}
};

PTTableProperty::PTTableProperty(MemoryContext *memctx,
                   YBLocation::SharedPtr loc,
                   const MCString::SharedPtr& lhs,
                   const PTExpr::SharedPtr& rhs)
    : TreeNode(memctx, loc),
      lhs_(lhs),
      rhs_(rhs) {
}

PTTableProperty::~PTTableProperty() {
}

CHECKED_STATUS PTTableProperty::Analyze(SemContext *sem_context) {

  // Verify we have a valid property name in the lhs.
  auto iterator = kPropertyDataTypes.find(lhs_->c_str());
  if (iterator == kPropertyDataTypes.end()) {
    return sem_context->Error(loc(), ErrorCode::INVALID_TABLE_PROPERTY);
  }

  if (!sem_context->IsConvertible(iterator->second, rhs_->sql_type())) {
    return sem_context->Error(loc(), ErrorCode::DATATYPE_MISMATCH);
  }

  if ((*iterator).first == kDefaultTimeToLive) {
    // TTL value is entered by user in seconds, but we store internally in milliseconds.
    if (!yb::common::isValidTTLSeconds(std::dynamic_pointer_cast<PTConstInt>(rhs_)->Eval())) {
      return sem_context->Error(loc(),
                                strings::Substitute("Valid ttl range : [$0, $1]",
                                                    yb::common::kMinTtlSeconds,
                                                    yb::common::kMaxTtlSeconds).c_str(),
                                ErrorCode::INVALID_ARGUMENTS);
    }
  }

  return Status::OK();
}

void PTTableProperty::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

CHECKED_STATUS PTTablePropertyListNode::Analyze(SemContext *sem_context) {
  // Set to ensure we don't have duplicate table properties.
  std::set<string> table_properties;
  for (PTTableProperty::SharedPtr tnode : node_list()) {
    string table_property_name = tnode->lhs()->c_str();
    if (table_properties.find(table_property_name) != table_properties.end()) {
      return sem_context->Error(loc(), ErrorCode::DUPLICATE_TABLE_PROPERTY);
    }
    RETURN_NOT_OK(tnode->Analyze(sem_context));
    table_properties.insert(table_property_name);
  }
  return Status::OK();
}

} // namespace sql
} // namespace yb
