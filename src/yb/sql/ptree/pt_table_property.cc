// Copyright (c) YugaByte, Inc.

#include "yb/sql/ptree/pt_table_property.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

// These property names need to be lowercase, since we they are all converted to lowercase after
// the parser/scanner phase and as a result if we're doing string matching everything should be
// lowercase.
const char PTTableProperty::kDefaultTimeToLive[] = "default_time_to_live";
const std::map<std::string, DataType> PTTableProperty::kPropertyDataTypes
    = {
    {PTTableProperty::kDefaultTimeToLive, DataType::INT64}
};

PTTableProperty::PTTableProperty(MemoryContext *memctx,
                   YBLocation::SharedPtr loc,
                   const MCString::SharedPtr& lhs,
                   const PTExpr::SharedPtr& rhs)
    : TreeNode(memctx, loc),
      lhs_(lhs),
      rhs_(rhs),
      property_type_(PropertyType::kTableProperty) {
}

PTTableProperty::PTTableProperty(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc,
                                 const MCString::SharedPtr& name,
                                 const PTOrderBy::Direction direction)
    : TreeNode(memctx, loc), name_(name), direction_(direction),
      property_type_(PropertyType::kClusteringOrder) {}

PTTableProperty::~PTTableProperty() {
}

CHECKED_STATUS PTTableProperty::Analyze(SemContext *sem_context) {

  // Verify we have a valid property name in the lhs.
  auto iterator = kPropertyDataTypes.find(lhs_->c_str());
  if (iterator == kPropertyDataTypes.end()) {
    return sem_context->Error(loc(), ErrorCode::INVALID_TABLE_PROPERTY);
  }

  if (!sem_context->IsConvertible(rhs_, YQLType(iterator->second))) {
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
  unordered_map<string, PTTableProperty::SharedPtr> order_tnodes;
  vector<string> order_columns;
  for (PTTableProperty::SharedPtr tnode : node_list()) {
    if (tnode->property_type() == PropertyType::kTableProperty) {
      string table_property_name = tnode->lhs()->c_str();
      if (table_properties.find(table_property_name) != table_properties.end()) {
        return sem_context->Error(loc(), ErrorCode::DUPLICATE_TABLE_PROPERTY);
      }
      RETURN_NOT_OK(tnode->Analyze(sem_context));
      table_properties.insert(table_property_name);
    } else if (tnode->property_type() == PropertyType::kClusteringOrder) {
      const auto& column_name = tnode->name().c_str();
      // Insert column_name only the first time we see it.
      if (order_tnodes.find(column_name) == order_tnodes.end()) {
        order_columns.push_back(tnode->name().c_str());
      }
      // If a column ordering was set more than once, we use the last order provided.
      order_tnodes[column_name] = tnode;
    }
  }

  auto order_column_iter = order_columns.begin();
  for (auto &pc : sem_context->current_table()->primary_columns()) {
    if (order_column_iter == order_columns.end()) {
      break;
    }
    const auto &tnode = order_tnodes[*order_column_iter];
    if (strcmp(pc->yb_name(), order_column_iter->c_str()) != 0) {
      string msg;
      // If we can bind pc->yb_name() in the order by list, it means the order of the columns is
      // incorrect.
      if (order_tnodes.find(pc->yb_name()) != order_tnodes.end()) {
        msg = strings::Substitute("Bad Request: The order of columns in the CLUSTERING "
            "ORDER directive must be the one of the clustering key ($0 must appear before $1)",
            pc->yb_name(), *order_column_iter);
      } else {
        msg = strings::Substitute("Bad Request: Missing CLUSTERING ORDER for column $0",
                                  pc->yb_name());
      }
      return sem_context->Error(tnode->loc(), msg.c_str(), ErrorCode::INVALID_TABLE_PROPERTY);
    }
    if(tnode->direction() == PTOrderBy::Direction::kASC) {
      pc->set_sorting_type(ColumnSchema::SortingType::kAscending);
    } else if (tnode->direction() == PTOrderBy::Direction::kDESC) {
      pc->set_sorting_type(ColumnSchema::SortingType::kDescending);
    }
    ++order_column_iter;
  }
  if (order_column_iter != order_columns.end()) {
    const auto &tnode = order_tnodes[*order_column_iter];
    auto msg = strings::Substitute(
        "Bad Request: Only clustering key columns can be defined in CLUSTERING ORDER directive");
    return sem_context->Error(tnode->loc(), msg.c_str(), ErrorCode::INVALID_TABLE_PROPERTY);
  }
  return Status::OK();
}

} // namespace sql
} // namespace yb
