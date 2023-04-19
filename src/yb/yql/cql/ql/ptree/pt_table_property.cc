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

#include "yb/yql/cql/ql/ptree/pt_table_property.h"

#include <set>

#include <boost/algorithm/string/predicate.hpp>

#include "yb/client/schema.h"
#include "yb/client/table.h"

#include "yb/common/schema.h"
#include "yb/common/table_properties_constants.h"

#include "yb/gutil/casts.h"

#include "yb/util/stol_utils.h"
#include "yb/util/string_case.h"
#include "yb/util/string_util.h"

#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_alter_table.h"
#include "yb/yql/cql/ql/ptree/pt_column_definition.h"
#include "yb/yql/cql/ql/ptree/pt_create_table.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

using std::string;
using std::ostream;
using std::vector;

namespace yb {
namespace ql {

namespace {

const std::string kCompactionClassPrefix = "org.apache.cassandra.db.compaction.";

}

using strings::Substitute;

// These property names need to be lowercase, since identifiers are converted to lowercase by the
// scanner phase and as a result if we're doing string matching everything should be lowercase.
const std::map<std::string, PTTableProperty::KVProperty> PTTableProperty::kPropertyDataTypes
    = {
    {"bloom_filter_fp_chance", KVProperty::kBloomFilterFpChance},
    {"caching", KVProperty::kCaching},
    {"comment", KVProperty::kComment},
    {"compaction", KVProperty::kCompaction},
    {"compression", KVProperty::kCompression},
    {"crc_check_chance", KVProperty::kCrcCheckChance},
    {"dclocal_read_repair_chance", KVProperty::kDclocalReadRepairChance},
    {"default_time_to_live", KVProperty::kDefaultTimeToLive},
    {"gc_grace_seconds", KVProperty::kGcGraceSeconds},
    {"index_interval", KVProperty::kIndexInterval},
    {"memtable_flush_period_in_ms", KVProperty::kMemtableFlushPeriodInMs},
    {"min_index_interval", KVProperty::kMinIndexInterval},
    {"max_index_interval", KVProperty::kMaxIndexInterval},
    {"read_repair_chance", KVProperty::kReadRepairChance},
    {"speculative_retry", KVProperty::kSpeculativeRetry},
    {"transactions", KVProperty::kTransactions},
    {"tablets", KVProperty::kNumTablets}
};

PTTableProperty::PTTableProperty(MemoryContext *memctx,
                                 YBLocationPtr loc,
                                 const MCSharedPtr<MCString>& lhs,
                                 const PTExprPtr& rhs)
    : PTProperty(memctx, loc, lhs, rhs),
      property_type_(PropertyType::kTableProperty) {}

PTTableProperty::PTTableProperty(MemoryContext *memctx,
                                 YBLocationPtr loc,
                                 const PTExprPtr& expr,
                                 const PTOrderBy::Direction direction)
    : PTProperty(memctx, loc), order_expr_(expr), direction_(direction),
      property_type_(PropertyType::kClusteringOrder) {}

PTTableProperty::PTTableProperty(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc)
    : PTProperty(memctx, loc) {
}

PTTableProperty::~PTTableProperty() {
}


Status PTTableProperty::AnalyzeSpeculativeRetry(const string &val) {
  string generic_error = Substitute("Invalid value $0 for option 'speculative_retry'", val);

  // Accepted values: ALWAYS, Xpercentile, Nms, NONE.
  if (val == common::kSpeculativeRetryAlways || val == common::kSpeculativeRetryNone) {
    return Status::OK();
  }

  string numeric_val;
  if (StringEndsWith(val, common::kSpeculativeRetryMs, common::kSpeculativeRetryMsLen,
                     &numeric_val)) {
    RETURN_NOT_OK(CheckedStold(numeric_val));
    return Status::OK();
  }

  if (StringEndsWith(val, common::kSpeculativeRetryPercentile,
                     common::kSpeculativeRetryPercentileLen, &numeric_val)) {
    auto percentile = CheckedStold(numeric_val);
    RETURN_NOT_OK(percentile);

    if (*percentile < 0.0 || *percentile > 100.0) {
      return STATUS(InvalidArgument, Substitute(
          "Invalid value $0 for PERCENTILE option 'speculative_retry': "
          "must be between 0.0 and 100.0", numeric_val));
    }
    return Status::OK();
  }
  return STATUS(InvalidArgument, generic_error);
}

string PTTableProperty::name() const {
  DCHECK_EQ(property_type_, PropertyType::kClusteringOrder);
  return order_expr_->QLName();
}

Status PTTableProperty::Analyze(SemContext *sem_context) {
  // Verify we have a valid property name in the lhs.
  const auto& table_property_name = lhs_->c_str();
  auto iterator = kPropertyDataTypes.find(table_property_name);
  if (iterator == kPropertyDataTypes.end()) {
    return sem_context->Error(this, Substitute("Unknown property '$0'", lhs_->c_str()).c_str(),
                              ErrorCode::INVALID_TABLE_PROPERTY);
  }

  long double double_val;
  int64_t int_val;
  string str_val;

  switch (iterator->second) {
    case KVProperty::kBloomFilterFpChance:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(GetDoubleValueFromExpr(rhs_, table_property_name,
                                                             &double_val));
      if (double_val <= 0.0 || double_val > 1.0) {
        return sem_context->Error(this,
            Substitute("$0 must be larger than 0 and less than or equal to 1.0 (got $1)",
                       table_property_name, std::to_string(double_val)).c_str(),
            ErrorCode::INVALID_ARGUMENTS);
      }
      break;
    case KVProperty::kCrcCheckChance: FALLTHROUGH_INTENDED;
    case KVProperty::kDclocalReadRepairChance: FALLTHROUGH_INTENDED;
    case KVProperty::kReadRepairChance:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(GetDoubleValueFromExpr(rhs_, table_property_name,
                                                             &double_val));
      if (double_val < 0.0 || double_val > 1.0) {
        return sem_context->Error(this,
            Substitute(
                "$0 must be larger than or equal to 0 and smaller than or equal to 1.0 (got $1)",
                table_property_name, std::to_string(double_val)).c_str(),
            ErrorCode::INVALID_ARGUMENTS);
      }
      break;
    case KVProperty::kDefaultTimeToLive:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(GetIntValueFromExpr(rhs_, table_property_name, &int_val));
      // TTL value is entered by user in seconds, but we store internally in milliseconds.
      if (!common::IsValidTTLSeconds(int_val)) {
        return sem_context->Error(this, Substitute("Valid ttl range : [$0, $1]",
                                                   common::kCassandraMinTtlSeconds,
                                                   common::kCassandraMaxTtlSeconds).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
      }
      break;
    case KVProperty::kGcGraceSeconds: FALLTHROUGH_INTENDED;
    case KVProperty::kMemtableFlushPeriodInMs:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(GetIntValueFromExpr(rhs_, table_property_name, &int_val));
      if (int_val < 0) {
        return sem_context->Error(this,
                                  Substitute("$0 must be greater than or equal to 0 (got $1)",
                                             table_property_name, std::to_string(int_val)).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
      }
      break;
    case KVProperty::kIndexInterval: FALLTHROUGH_INTENDED;
    case KVProperty::kMinIndexInterval: FALLTHROUGH_INTENDED;
    case KVProperty::kMaxIndexInterval:
      // TODO(hector): Check that kMaxIndexInterval is greater than kMinIndexInterval.
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(GetIntValueFromExpr(rhs_, table_property_name, &int_val));
      if (int_val < 1) {
        return sem_context->Error(this,
                                  Substitute("$0 must be greater than or equal to 1 (got $1)",
                                             table_property_name, std::to_string(int_val)).c_str(),
                                  ErrorCode::INVALID_ARGUMENTS);
      }
      break;
    case KVProperty::kSpeculativeRetry:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(GetStringValueFromExpr(rhs_, true, table_property_name,
                                                             &str_val));
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(AnalyzeSpeculativeRetry(str_val));
      break;
    case KVProperty::kComment:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(GetStringValueFromExpr(rhs_, true, table_property_name,
                                                             &str_val));
      break;
    case KVProperty::kCompaction: FALLTHROUGH_INTENDED;
    case KVProperty::kCaching: FALLTHROUGH_INTENDED;
    case KVProperty::kCompression: FALLTHROUGH_INTENDED;
    case KVProperty::kTransactions:
      return sem_context->Error(this,
                                Substitute("Invalid value for option '$0'. Value must be a map",
                                           table_property_name).c_str(),
                                ErrorCode::DATATYPE_MISMATCH);
    case KVProperty::kNumTablets:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(GetIntValueFromExpr(rhs_, table_property_name, &int_val));
      if (int_val < 0) {
        return sem_context->Error(
            this, "Number of tablets cannot be less zero", ErrorCode::INVALID_ARGUMENTS);
      }
      if (int_val > FLAGS_max_num_tablets_for_table) {
        return sem_context->Error(
            this, "Number of tablets exceeds system limit", ErrorCode::INVALID_ARGUMENTS);
      }
      break;
  }

  PTAlterTable *alter_table = sem_context->current_alter_table();
  if (alter_table != nullptr) {
    // Some table properties are not supported in ALTER TABLE.
    if (iterator->second == KVProperty::kNumTablets) {
      return sem_context->Error(this,
                                "Changing the number of tablets is not supported",
                                ErrorCode::FEATURE_NOT_SUPPORTED);
    }

    RETURN_NOT_OK(alter_table->AppendAlterProperty(sem_context, this));
  }
  return Status::OK();
}

std::ostream& operator<<(ostream& os, const PropertyType& property_type) {
  switch(property_type) {
    case PropertyType::kTableProperty:
      os << "kTableProperty";
      break;
    case PropertyType::kClusteringOrder:
      os << "kClusteringOrder";
      break;
    case PropertyType::kTablePropertyMap:
      os << "kTablePropertyMap";
      break;
  }
  return os;
}

void PTTableProperty::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

Status PTTablePropertyListNode::Analyze(SemContext *sem_context) {
  // Set to ensure we don't have duplicate table properties.
  std::set<string> table_properties;
  std::unordered_map<string, PTTableProperty::SharedPtr> order_tnodes;
  vector<string> order_columns;
  for (PTTableProperty::SharedPtr tnode : node_list()) {
    if (tnode == nullptr) {
      // This shouldn't happen because AppendList ignores null nodes.
      LOG(ERROR) << "Invalid null property";
      continue;
      }
    switch(tnode->property_type()) {
      case PropertyType::kTableProperty: FALLTHROUGH_INTENDED;
      case PropertyType::kTablePropertyMap: {
        string table_property_name = tnode->lhs()->c_str();
        if (table_properties.find(table_property_name) != table_properties.end()) {
          return sem_context->Error(this, ErrorCode::DUPLICATE_TABLE_PROPERTY);
        }
        RETURN_NOT_OK(tnode->Analyze(sem_context));
        table_properties.insert(table_property_name);
        break;
      }
      case PropertyType::kClusteringOrder: {
        const MCString column_name(tnode->name().c_str(), sem_context->PTempMem());
        const PTColumnDefinition *col = sem_context->GetColumnDefinition(column_name);
        if (col == nullptr || !col->is_primary_key() || col->is_hash_key()) {
          return sem_context->Error(tnode, "Not a clustering key column",
                                    ErrorCode::INVALID_TABLE_PROPERTY);
        }
        // Insert column_name only the first time we see it.
        if (order_tnodes.find(column_name.c_str()) == order_tnodes.end()) {
          order_columns.push_back(column_name.c_str());
        }
        // If a column ordering was set more than once, we use the last order provided.
        order_tnodes[column_name.c_str()] = tnode;
        break;
      }
    }
  }

  auto order_column_iter = order_columns.begin();
  for (auto &pc : sem_context->current_create_table_stmt()->primary_columns()) {
    if (order_column_iter == order_columns.end()) {
      break;
    }
    const auto &tnode = order_tnodes[*order_column_iter];
    if (strcmp(pc->yb_name(), order_column_iter->c_str()) != 0) {
      string msg;
      // If we can find pc->yb_name() in the order-by list, it means the order of the columns is
      // incorrect.
      if (order_tnodes.find(pc->yb_name()) != order_tnodes.end()) {
        msg = Substitute("Columns in the CLUSTERING ORDER directive must be in same order as "
                         "the clustering key columns order ($0 must appear before $1)",
                         pc->yb_name(), *order_column_iter);
      } else {
        msg = Substitute("Missing CLUSTERING ORDER for column $0", pc->yb_name());
      }
      return sem_context->Error(tnode, msg.c_str(), ErrorCode::INVALID_TABLE_PROPERTY);
    }
    if (tnode->direction() == PTOrderBy::Direction::kASC) {
      pc->set_sorting_type(SortingType::kAscending);
    } else if (tnode->direction() == PTOrderBy::Direction::kDESC) {
      pc->set_sorting_type(SortingType::kDescending);
    }
    ++order_column_iter;
  }
  if (order_column_iter != order_columns.end()) {
    const auto &tnode = order_tnodes[*order_column_iter];
    return sem_context->Error(tnode,
                              "Only clustering key columns can be defined in "
                              "CLUSTERING ORDER directive", ErrorCode::INVALID_TABLE_PROPERTY);
  }
  return Status::OK();
}

Status PTTableProperty::SetTableProperty(yb::TableProperties *table_property) const {
  // TODO: Also reject properties that cannot be changed during alter table (like clustering order)

  // Clustering order not handled here.
  if (property_type_ == PropertyType::kClusteringOrder) {
    return Status::OK();
  }

  string table_property_name;
  ToLowerCase(lhs_->c_str(), &table_property_name);
  auto iterator = kPropertyDataTypes.find(table_property_name);
  if (iterator == kPropertyDataTypes.end()) {
    return STATUS(InvalidArgument, Substitute("$0 is not a valid table property", lhs_->c_str()));
  }
  switch (iterator->second) {
    case KVProperty::kDefaultTimeToLive: {
      // TTL value is entered by user in seconds, but we store internally in milliseconds.
      int64_t val;
      if (!GetIntValueFromExpr(rhs_, table_property_name, &val).ok()) {
        return STATUS(InvalidArgument, Substitute("Invalid value for default_time_to_live"));
      }
      table_property->SetDefaultTimeToLive(val * MonoTime::kMillisecondsPerSecond);
      break;
    }
    case KVProperty::kBloomFilterFpChance: FALLTHROUGH_INTENDED;
    case KVProperty::kComment: FALLTHROUGH_INTENDED;
    case KVProperty::kCrcCheckChance: FALLTHROUGH_INTENDED;
    case KVProperty::kDclocalReadRepairChance: FALLTHROUGH_INTENDED;
    case KVProperty::kGcGraceSeconds: FALLTHROUGH_INTENDED;
    case KVProperty::kIndexInterval: FALLTHROUGH_INTENDED;
    case KVProperty::kMemtableFlushPeriodInMs: FALLTHROUGH_INTENDED;
    case KVProperty::kMinIndexInterval: FALLTHROUGH_INTENDED;
    case KVProperty::kMaxIndexInterval: FALLTHROUGH_INTENDED;
    case KVProperty::kReadRepairChance: FALLTHROUGH_INTENDED;
    case KVProperty::kSpeculativeRetry:
      LOG(WARNING) << "Ignoring table property " << table_property_name;
      break;
    case KVProperty::kCaching: FALLTHROUGH_INTENDED;
    case KVProperty::kCompaction: FALLTHROUGH_INTENDED;
    case KVProperty::kCompression: FALLTHROUGH_INTENDED;
    case KVProperty::kTransactions:
      LOG(ERROR) << "Not primitive table property " << table_property_name;
      break;
    case KVProperty::kNumTablets:
      int64_t val;
      auto status = GetIntValueFromExpr(rhs_, table_property_name, &val);
      if (!status.ok()) {
        return status.CloneAndAppend("Invalid value for tablets");
      }
      table_property->SetNumTablets(trim_cast<int32_t>(val));
      break;
  }
  return Status::OK();
}

const std::map<string, PTTablePropertyMap::PropertyMapType> PTTablePropertyMap::kPropertyDataTypes
    = {
    {"caching", PTTablePropertyMap::PropertyMapType::kCaching},
    {"compaction", PTTablePropertyMap::PropertyMapType::kCompaction},
    {"compression", PTTablePropertyMap::PropertyMapType::kCompression},
    {"transactions", PTTablePropertyMap::PropertyMapType::kTransactions}
};

PTTablePropertyMap::PTTablePropertyMap(MemoryContext *memctx,
                                       YBLocationPtr loc)
    : PTTableProperty(memctx, loc) {
  property_type_ = PropertyType::kTablePropertyMap;
  map_elements_ = TreeListNode<PTTableProperty>::MakeShared(memctx, loc);
}

PTTablePropertyMap::~PTTablePropertyMap() {
}

Status PTTablePropertyMap::Analyze(SemContext *sem_context) {
  // Verify we have a valid property name in the lhs.
  const auto &property_name = lhs_->c_str();
  auto iterator = kPropertyDataTypes.find(property_name);
  if (iterator == kPropertyDataTypes.end()) {
    if (IsValidProperty(property_name)) {
      return sem_context->Error(this, Substitute("Invalid map value for property '$0'",
                                                 property_name).c_str(),
                                ErrorCode::DATATYPE_MISMATCH);
    }
    return sem_context->Error(this, Substitute("Unknown property '$0'", property_name).c_str(),
                              ErrorCode::INVALID_TABLE_PROPERTY);
  }

  const auto &property_type = iterator->second;

  switch (property_type) {
    case PropertyMapType::kCaching:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(AnalyzeCaching());
      break;
    case PropertyMapType::kCompaction:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(AnalyzeCompaction());
      break;
    case PropertyMapType::kCompression:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(AnalyzeCompression());
      break;
    case PropertyMapType::kTransactions:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(AnalyzeTransactions(sem_context));
      break;
  }
  return Status::OK();
}

void PTTablePropertyMap::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

Status PTTablePropertyMap::SetTableProperty(yb::TableProperties *table_property) const {
  string table_property_name;
  ToLowerCase(lhs_->c_str(), &table_property_name);
  auto iterator = kPropertyDataTypes.find(table_property_name);
  if (iterator == kPropertyDataTypes.end()) {
    return STATUS(InvalidArgument, Substitute("$0 is not a valid table property", lhs_->c_str()));
  }
  switch (iterator->second) {
    case PropertyMapType::kCaching: FALLTHROUGH_INTENDED;
    case PropertyMapType::kCompaction: FALLTHROUGH_INTENDED;
    case PropertyMapType::kCompression:
      LOG(WARNING) << "Ignoring table property " << table_property_name;
      break;
    case PropertyMapType::kTransactions:
      for (const auto& subproperty : map_elements_->node_list()) {
        string subproperty_name;
        ToLowerCase(subproperty->lhs()->c_str(), &subproperty_name);
        auto iter = Transactions::kSubpropertyDataTypes.find(subproperty_name);
        DCHECK(iter != Transactions::kSubpropertyDataTypes.end());
        bool bool_val;
        string str_val;
        switch(iter->second) {
          case Transactions::Subproperty::kEnabled:
            RETURN_NOT_OK(GetBoolValueFromExpr(subproperty->rhs(), subproperty_name, &bool_val));
            table_property->SetTransactional(bool_val);
            break;
          case Transactions::Subproperty::kConsistencyLevel:
            RETURN_NOT_OK(
                GetStringValueFromExpr(subproperty->rhs(), true, subproperty_name, &str_val));
            if (str_val == Transactions::kConsistencyLevelUserEnforced) {
              table_property->SetConsistencyLevel(YBConsistencyLevel::USER_ENFORCED);
            }
            break;
        }
      }
      break;
  }
  return Status::OK();
}

Status PTTablePropertyMap::AnalyzeCompaction() {
  vector<string> invalid_subproperties;
  vector<PTTableProperty::SharedPtr> subproperties;
  auto class_subproperties_iter = Compaction::kClassSubproperties.end();
  string class_name;
  for (PTTableProperty::SharedPtr tnode : map_elements_->node_list()) {
    string subproperty_name;
    ToLowerCase(tnode->lhs()->c_str(), &subproperty_name);
    // 'class' is a special compaction subproperty. It tell us which other subproperties are valid.
    // We intentionally don't check if class_name is non_empty. If several 'class' subproperties
    // have been set, we use the last one.
    if (subproperty_name == "class") {
      if (tnode->rhs()->ql_type_id() != DataType::STRING) {
        return STATUS(InvalidArgument,
                      "Property value for property 'class' has and invalid data type");
      }
      class_name = std::dynamic_pointer_cast<PTConstText>(tnode->rhs())->Eval()->c_str();
      if (class_name.find('.') == string::npos) {
        if (!boost::starts_with(class_name, kCompactionClassPrefix)) {
          LOG(INFO) << "Inserting prefix into class name";
          class_name.insert(0, kCompactionClassPrefix);
        }
      }
      class_subproperties_iter = Compaction::kClassSubproperties.find(class_name);
      if (class_subproperties_iter == Compaction::kClassSubproperties.end()) {
        return STATUS(InvalidArgument,
                      Substitute("Unable to find compaction strategy class '$0'", class_name));
      }
      continue;
    }
    auto iter = Compaction::kSubpropertyDataTypes.find(subproperty_name);
    if (iter == Compaction::kSubpropertyDataTypes.end()) {
      invalid_subproperties.push_back(subproperty_name);
    } else {
      // iter->second is of type std::pair<CompactionSubproperty, DataType>.
      subproperties.push_back(tnode);
    }
  }
  LOG(INFO) << "AnalyzeCompaction 2";

  if (class_subproperties_iter == Compaction::kClassSubproperties.end()) {
    LOG(INFO) << "AnalyzeCompaction 3";
    return STATUS(InvalidArgument, "Missing sub-option 'class' for the 'compaction' option");
  }

  // Verify that the types for subproperties values are valid. Also verify that elements in
  // subproperties are valid for the given class.
  auto const& valid_subproperties_for_class = class_subproperties_iter->second;
  long double bucket_high = 1.5, bucket_low = 0.5;
  int64_t max_threshold = 32, min_threshold = 4;
  for (const auto& subproperty : subproperties) {
    string subproperty_name;
    ToLowerCase(subproperty->lhs()->c_str(), &subproperty_name);
    auto subproperty_enum = Compaction::kSubpropertyDataTypes.at(subproperty_name);
    if (valid_subproperties_for_class.find(subproperty_enum) ==
        valid_subproperties_for_class.end()) {
      invalid_subproperties.push_back(subproperty_name);
      continue;
    }
    // Even if we already know that we have a subproperty that is not valid for a given class,
    // Cassandra will first complain about invalid values for a valid subproperty.
    int64_t int_val;
    long double double_val;
    bool bool_val;
    string str_val;
    switch(subproperty_enum) {
      case Compaction::Subproperty::kBaseTimeSeconds: FALLTHROUGH_INTENDED;
      case Compaction::Subproperty::kCompactionWindowSize: FALLTHROUGH_INTENDED;
      case Compaction::Subproperty::kSstableSizeInMb: FALLTHROUGH_INTENDED;
      case Compaction::Subproperty::kTombstoneCompactionInterval:
        RETURN_NOT_OK(GetIntValueFromExpr(subproperty->rhs(), subproperty_name, &int_val));
        if (int_val <= 0) {
          return STATUS(InvalidArgument, Substitute("$0 must be greater than 0, but was $1",
                                                    subproperty_name, std::to_string(int_val)));
        }
        break;
      case Compaction::Subproperty::kBucketHigh:
        RETURN_NOT_OK(GetDoubleValueFromExpr(subproperty->rhs(), subproperty_name, &bucket_high));
        break;
      case Compaction::Subproperty::kBucketLow:
        RETURN_NOT_OK(GetDoubleValueFromExpr(subproperty->rhs(), subproperty_name, &bucket_low));
        break;
      case Compaction::Subproperty::kClass:
        LOG(FATAL) << "Invalid subproperty";
      case Compaction::Subproperty::kCompactionWindowUnit:
        RETURN_NOT_OK(GetStringValueFromExpr(subproperty->rhs(), true, subproperty_name, &str_val));
        if (Compaction::kWindowUnits.find(str_val) ==
            Compaction::kWindowUnits.end()) {
          return STATUS(InvalidArgument,
                        Substitute("$0 is not valid for '$1'", str_val, subproperty_name));
        }
        break;
      case Compaction::Subproperty::kEnabled: FALLTHROUGH_INTENDED;
      case Compaction::Subproperty::kLogAll: FALLTHROUGH_INTENDED;
      case Compaction::Subproperty::kOnlyPurgeRepairedTombstones: FALLTHROUGH_INTENDED;
      case Compaction::Subproperty::kUncheckedTombstoneCompaction:
        RETURN_NOT_OK(GetBoolValueFromExpr(subproperty->rhs(), subproperty_name, &bool_val));
        break;
      case Compaction::Subproperty::kMaxSstableAgeDays:
        RETURN_NOT_OK(GetDoubleValueFromExpr(subproperty->rhs(), subproperty_name, &double_val));
        if (double_val < 0) {
          return STATUS(InvalidArgument, Substitute("$0 must be non-negative, but was $1",
                                                    subproperty_name, std::to_string(double_val)));
        }
        break;;
      case Compaction::Subproperty::kMaxThreshold:
        RETURN_NOT_OK(GetIntValueFromExpr(subproperty->rhs(), subproperty_name, &max_threshold));
        break;
      case Compaction::Subproperty::kMaxWindowSizeSeconds: FALLTHROUGH_INTENDED;
      case Compaction::Subproperty::kMinSstableSize:
        RETURN_NOT_OK(GetIntValueFromExpr(subproperty->rhs(), subproperty_name, &int_val));
        if (int_val < 0) {
          return STATUS(InvalidArgument, Substitute("$0 must be non-negative, but was $1",
                                                    subproperty_name, std::to_string(int_val)));
        }
        break;
      case Compaction::Subproperty::kMinThreshold:
        RETURN_NOT_OK(GetIntValueFromExpr(subproperty->rhs(), subproperty_name, &min_threshold));
        if (min_threshold <= 0) {
          return STATUS(InvalidArgument,
                        "Disabling compaction by setting compaction thresholds to 0 has been "
                            "removed, set the compaction option 'enabled' to false instead");
        } else if (min_threshold < 2) {
          return STATUS(InvalidArgument,
                        Substitute("Min compaction threshold cannot be less than 2 (got $0)",
                                   min_threshold));
        }
        break;
      case Compaction::Subproperty::kTimestampResolution:
        RETURN_NOT_OK(GetStringValueFromExpr(subproperty->rhs(), true, subproperty_name, &str_val));
        if (Compaction::kTimestampResolutionUnits.find(str_val) ==
            Compaction::kTimestampResolutionUnits.end()) {
          return STATUS(InvalidArgument,
                        Substitute("$0 is not valid for '$1'", str_val, subproperty_name));
        }
        break;
      case Compaction::Subproperty::kTombstoneThreshold:
        RETURN_NOT_OK(GetDoubleValueFromExpr(subproperty->rhs(), subproperty_name, &double_val));
        if (double_val <= 0) {
          return STATUS(InvalidArgument, Substitute("$0 must be greater than 0, but was $1",
                                                    subproperty_name, std::to_string(double_val)));
        }
        break;
    }
  }

  if (bucket_high <= bucket_low) {
    return STATUS(InvalidArgument, Substitute("'bucket_high' value ($0) is less than or equal to "
        "'bucket_low' value ($1)", std::to_string(bucket_high), std::to_string(bucket_low)));
  }

  if (max_threshold <= min_threshold) {
    return STATUS(InvalidArgument, Substitute("'max_threshold' value ($0) is less than or equal to "
        "'min_threshold' value ($1)", std::to_string(max_threshold),
        std::to_string(min_threshold)));
  }

  if (!invalid_subproperties.empty()) {
    return STATUS_FORMAT(InvalidArgument, "Properties specified $0 are not understood by $1",
                         invalid_subproperties, class_name);
  }
  return Status::OK();
}

Status PTTablePropertyMap::AnalyzeCaching() {
  string str_val;
  int64_t int_val;
  for (const auto& subproperty : map_elements_->node_list()) {
    string subproperty_name;
    ToLowerCase(subproperty->lhs()->c_str(), &subproperty_name);
    if (subproperty_name == common::kCachingKeys) {
      // First try to read the value as a string.
      RETURN_NOT_OK(GetStringValueFromExpr(subproperty->rhs(), true, subproperty_name, &str_val));
      if (common::IsValidCachingKeysString(str_val)) {
        continue;
      }
      return STATUS(InvalidArgument, Substitute("Invalid value for caching sub-option '$0': only "
          "'$1' and '$2' are allowed", common::kCachingKeys, common::kCachingAll,
          common::kCachingNone));
    } else if (subproperty_name == common::kCachingRowsPerPartition) {
      if (GetStringValueFromExpr(subproperty->rhs(), true, subproperty_name, &str_val).ok()) {
        if (common::IsValidCachingRowsPerPartitionString(str_val)) {
          continue;
        }
      }
      if (GetIntValueFromExpr(subproperty->rhs(), subproperty_name, &int_val).ok()) {
        if (common::IsValidCachingRowsPerPartitionInt(int_val)) {
          continue;
        }
      }
      return STATUS(InvalidArgument, Substitute("Invalid value for caching sub-option '$0': only "
          "'$1', '$2' and integer values are allowed", common::kCachingRowsPerPartition,
          common::kCachingAll, common::kCachingNone));
    }
    return STATUS(InvalidArgument, Substitute("Invalid caching sub-options $0: only '$1' and "
        "'$2' are allowed", subproperty_name, common::kCachingKeys,
        common::kCachingRowsPerPartition));
  }
  return Status::OK();
}

Status PTTablePropertyMap::AnalyzeCompression() {
  string class_name, sstable_compression;
  bool has_sstable_compression = false;
  // 'class' and 'sstable_compression' are treated differently.
  for (const auto& subproperty : map_elements_->node_list()) {
    string subproperty_name;
    ToLowerCase(subproperty->lhs()->c_str(), &subproperty_name);
    if (subproperty_name == "class") {
      RETURN_NOT_OK(GetStringValueFromExpr(subproperty->rhs(), true, subproperty_name,
                                           &class_name));
      if (class_name.empty()) {
        return STATUS(InvalidArgument,
            "The 'class' option must not be empty. To disable compression use 'enabled' : false");
      }
    } else if (subproperty_name == "sstable_compression") {
      RETURN_NOT_OK(GetStringValueFromExpr(subproperty->rhs(), true, subproperty_name,
                                           &sstable_compression));
      has_sstable_compression = true;
    }
  }
  if (!class_name.empty() && has_sstable_compression) {
    return STATUS(InvalidArgument, "The 'sstable_compression' option must not be used if the "
        "compression algorithm is already specified by the 'class' option");
  }
  if (class_name.empty() && !has_sstable_compression) {
    return STATUS(InvalidArgument, "Missing sub-option 'class' for the 'compression' option");
  }

  for (const auto& subproperty : map_elements_->node_list()) {
    string subproperty_name;
    ToLowerCase(subproperty->lhs()->c_str(), &subproperty_name);
    auto iter = Compression::kSubpropertyDataTypes.find(subproperty_name);
    if (iter == Compression::kSubpropertyDataTypes.end()) {
      return STATUS(InvalidArgument, Substitute("Unknown compression option $0", subproperty_name));
    }

    int64_t int_val;
    long double double_val;
    bool bool_val;
    switch(iter->second) {
      case Compression::Subproperty::kChunkLengthKb:
        RETURN_NOT_OK(GetIntValueFromExpr(subproperty->rhs(), subproperty_name, &int_val));
        if (int_val > std::numeric_limits<int32_t>::max() / 1024) {
          return STATUS(InvalidArgument, Substitute("Value of $0 is too large ($1)",
                                                    subproperty_name, int_val));
        }
        break;
      case Compression::Subproperty::kClass:
        break;
      case Compression::Subproperty::kCrcCheckChance:
        RETURN_NOT_OK(GetDoubleValueFromExpr(subproperty->rhs(), subproperty_name, &double_val));
        if (double_val < 0.0 || double_val > 1.0) {
          return STATUS(InvalidArgument, Substitute("$0 should be between 0.0 and 1.0",
                                                    subproperty_name));
        }
        break;
      case Compression::Subproperty::kEnabled:
        RETURN_NOT_OK(GetBoolValueFromExpr(subproperty->rhs(), subproperty_name, &bool_val));
        break;
      case Compression::Subproperty::kSstableCompression:
        break;
    }
  }
  return Status::OK();
}

Status PTTablePropertyMap::AnalyzeTransactions(SemContext *sem_context) {
  for (const auto& subproperty : map_elements_->node_list()) {
    string subproperty_name;
    ToLowerCase(subproperty->lhs()->c_str(), &subproperty_name);
    auto iter = Transactions::kSubpropertyDataTypes.find(subproperty_name);
    if (iter == Transactions::kSubpropertyDataTypes.end()) {
      return STATUS(InvalidArgument, Substitute("Unknown transactions option $0",
                                                subproperty_name));
    }

    bool bool_val;
    string str_val;
    switch(iter->second) {
      case Transactions::Subproperty::kEnabled:
        RETURN_NOT_OK(GetBoolValueFromExpr(subproperty->rhs(), subproperty_name, &bool_val));
        break;
      case Transactions::Subproperty::kConsistencyLevel:
        if (sem_context->current_create_table_stmt()->opcode() != TreeNodeOpcode::kPTCreateIndex) {
          return STATUS(InvalidArgument,
                        Substitute("Unknown property '$0'", subproperty_name).c_str());
        }
        RETURN_NOT_OK(GetStringValueFromExpr(subproperty->rhs(), true, subproperty_name, &str_val));
        if (str_val != Transactions::kConsistencyLevelUserEnforced) {
          return STATUS(InvalidArgument,
                        Substitute("Invalid value for property '$0'", subproperty_name).c_str());
        }
        break;
    }
  }
  return Status::OK();
}

const std::map<std::string, Compression::Subproperty> Compression::kSubpropertyDataTypes = {
    {"chunk_length_kb",     Compression::Subproperty::kChunkLengthKb},
    {"chunk_length_in_kb",  Compression::Subproperty::kChunkLengthKb},
    {"class",               Compression::Subproperty::kClass},
    {"crc_check_chance",    Compression::Subproperty::kCrcCheckChance},
    {"enabled",             Compression::Subproperty::kEnabled},
    {"sstable_compression", Compression::Subproperty::kSstableCompression}
};

const std::map<std::string, Compaction::Subproperty> Compaction::kSubpropertyDataTypes = {
    {"base_time_seconds", Compaction::Subproperty::kBaseTimeSeconds},
    {"bucket_high", Compaction::Subproperty::kBucketHigh},
    {"bucket_low", Compaction::Subproperty::kBucketLow},
    {"class", Compaction::Subproperty::kClass},
    {"compaction_window_size", Compaction::Subproperty::kCompactionWindowSize},
    {"compaction_window_unit", Compaction::Subproperty::kCompactionWindowUnit},
    {"enabled", Compaction::Subproperty::kEnabled},
    {"log_all", Compaction::Subproperty::kLogAll},
    {"max_sstable_age_days", Compaction::Subproperty::kMaxSstableAgeDays},
    {"max_threshold", Compaction::Subproperty::kMaxThreshold},
    {"max_window_size_seconds", Compaction::Subproperty::kMaxWindowSizeSeconds},
    {"min_sstable_size", Compaction::Subproperty::kMinSstableSize},
    {"min_threshold", Compaction::Subproperty::kMinThreshold},
    {"only_purge_repaired_tombstones", Compaction::Subproperty::kOnlyPurgeRepairedTombstones},
    {"sstable_size_in_mb", Compaction::Subproperty::kSstableSizeInMb},
    {"timestamp_resolution", Compaction::Subproperty::kTimestampResolution},
    {"tombstone_compaction_interval", Compaction::Subproperty::kTombstoneCompactionInterval},
    {"tombstone_threshold", Compaction::Subproperty::kTombstoneThreshold},
    {"unchecked_tombstone_compaction", Compaction::Subproperty::kUncheckedTombstoneCompaction}
};

const std::map<std::string, Transactions::Subproperty>
Transactions::kSubpropertyDataTypes = {
    {"enabled", Transactions::Subproperty::kEnabled},
    {"consistency_level", Transactions::Subproperty::kConsistencyLevel}
};

const std::map<std::string, std::set<Compaction::Subproperty>> Compaction::kClassSubproperties = {
    {"org.apache.cassandra.db.compaction.SizeTieredCompactionStrategy",
        {
            Compaction::Subproperty::kBucketHigh,
            Compaction::Subproperty::kBucketLow,
            Compaction::Subproperty::kEnabled,
            Compaction::Subproperty::kLogAll,
            Compaction::Subproperty::kMaxThreshold,
            Compaction::Subproperty::kMinThreshold,
            Compaction::Subproperty::kMinSstableSize,
            Compaction::Subproperty::kOnlyPurgeRepairedTombstones,
            Compaction::Subproperty::kTombstoneCompactionInterval,
            Compaction::Subproperty::kTombstoneThreshold,
            Compaction::Subproperty::kUncheckedTombstoneCompaction
        }
    },
    {"org.apache.cassandra.db.compaction.DateTieredCompactionStrategy",
        {
            Compaction::Subproperty::kBaseTimeSeconds,
            Compaction::Subproperty::kEnabled,
            Compaction::Subproperty::kLogAll,
            Compaction::Subproperty::kMaxSstableAgeDays,
            Compaction::Subproperty::kMaxWindowSizeSeconds,
            Compaction::Subproperty::kMaxThreshold,
            Compaction::Subproperty::kMinThreshold,
            Compaction::Subproperty::kTimestampResolution,
            Compaction::Subproperty::kTombstoneCompactionInterval,
            Compaction::Subproperty::kTombstoneThreshold,
            Compaction::Subproperty::kUncheckedTombstoneCompaction
        }
    },
    {"org.apache.cassandra.db.compaction.LeveledCompactionStrategy",
        {
            Compaction::Subproperty::kEnabled,
            Compaction::Subproperty::kLogAll,
            Compaction::Subproperty::kSstableSizeInMb,
            Compaction::Subproperty::kTombstoneCompactionInterval,
            Compaction::Subproperty::kTombstoneThreshold,
            Compaction::Subproperty::kUncheckedTombstoneCompaction
        }
    },
    {"org.apache.cassandra.db.compaction.TimeWindowCompactionStrategy",
        {
            Compaction::Subproperty::kCompactionWindowUnit,
            Compaction::Subproperty::kCompactionWindowSize,
            Compaction::Subproperty::kLogAll
        }
    }
};

std::set<std::string> Compaction::kWindowUnits = {"minutes", "hours", "days"};
std::set<std::string> Compaction::kTimestampResolutionUnits = {
    "days", "hours", "microseconds", "milliseconds", "minutes", "nanoseconds", "seconds"
};


} // namespace ql
} // namespace yb
