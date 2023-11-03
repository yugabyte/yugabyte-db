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

#include "yb/master/yql_indexes_vtable.h"

#include "yb/qlexpr/index_column.h"
#include "yb/qlexpr/ql_name.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"

#include "yb/util/status_log.h"

using std::string;

namespace yb {
namespace master {

YQLIndexesVTable::YQLIndexesVTable(const TableName& table_name,
                                   const NamespaceName& namespace_name,
                                   Master* const master)
    : YQLVirtualTable(table_name, namespace_name, master, CreateSchema()) {
}

namespace {

const string& ColumnName(const Schema& schema, const ColumnId id) {
  auto column = schema.column_by_id(id);
  DCHECK(column.ok());
  return column->name();
}

string QLExpressionPBToPredicateString(const QLExpressionPB& where_expr, const Schema& schema) {
  // TODO(Piyush): Move this to some sort of util file and handle more cases if later we support
  // them in partial index predicate.
  switch (where_expr.expr_case()) {
    case yb::QLExpressionPB::EXPR_NOT_SET: return "NULL";
    case QLExpressionPB::kValue:
      return QLValue(where_expr.value()).ToValueString(QuotesType::kSingleQuotes);
    case QLExpressionPB::kColumnId: return ColumnName(schema, ColumnId(where_expr.column_id()));
    case QLExpressionPB::kCondition:
    {
      std::string res;
      res += QLExpressionPBToPredicateString(where_expr.condition().operands(0), schema);
      switch (where_expr.condition().op()) {
        case QLOperator::QL_OP_AND:
          res += " AND ";
          break;
        case QLOperator::QL_OP_EQUAL:
          res += " = ";
          break;
        case QLOperator::QL_OP_NOT_EQUAL:
          res += " != ";
          break;
        case QLOperator::QL_OP_GREATER_THAN:
          res += " > ";
          break;
        case QLOperator::QL_OP_GREATER_THAN_EQUAL:
          res += " >= ";
          break;
        case QLOperator::QL_OP_LESS_THAN:
          res += " < ";
          break;
        case QLOperator::QL_OP_LESS_THAN_EQUAL:
          res += " <= ";
          break;
        default:
          LOG_IF(DFATAL, false) << "We should have handled anything required.";
          break;
      }
      res += QLExpressionPBToPredicateString(where_expr.condition().operands(1), schema);
      return res;
    }
    default:
      LOG_IF(DFATAL, false) << "We should have handled anything required.";
  }
  return std::string();
}

} // namespace

Result<VTableDataPtr> YQLIndexesVTable::RetrieveData(
    const QLReadRequestPB& request) const {
  auto vtable = std::make_shared<qlexpr::QLRowBlock>(schema());
  auto* catalog_manager = &this->catalog_manager();

  auto tables = catalog_manager->GetTables(GetTablesMode::kVisibleToClient);
  for (const auto& table : tables) {
    const auto indexed_table_id = table->indexed_table_id();
    if (indexed_table_id.empty()) {
      continue;
    }

    // Skip non-YQL indexes.
    if (!IsYcqlTable(*table)) {
      continue;
    }

    scoped_refptr<TableInfo> indexed_table = catalog_manager->GetTableInfo(indexed_table_id);
    // Skip if the index is invalid (bad schema).
    if (indexed_table == nullptr) {
      continue;
    }
    Schema indexed_schema;
    RETURN_NOT_OK(indexed_table->GetSchema(&indexed_schema));

    // Get namespace for table.
    auto ns_info = VERIFY_RESULT(catalog_manager->FindNamespaceById(table->namespace_id()));

    // Create appropriate row for the table;
    auto& row = vtable->Extend();
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, ns_info->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kTableName, indexed_table->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kIndexName, table->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kKind, "COMPOSITES", &row));

    string target;
    auto index_info = indexed_table->GetIndexInfo(table->id());
    for (size_t i = 0; i < index_info.hash_column_count(); i++) {
      if (index_info.use_mangled_column_name()) {
        // Newer IndexInfo uses mangled name of expression instead of column ID of the table
        // that was indexed.
        target += qlexpr::YcqlName::DemangleName(index_info.columns()[i].column_name);
      } else {
        target += ColumnName(indexed_schema, index_info.columns()[i].indexed_column_id);
      }
      if (i != index_info.hash_column_count() - 1) {
        target += ", ";
      }
    }
    if (index_info.hash_column_count() > 1) {
      target = '(' + target + ')';
    }
    for (size_t i = index_info.hash_column_count();
         i < index_info.hash_column_count() + index_info.range_column_count(); i++) {
      target += ", ";
      if (index_info.use_mangled_column_name()) {
        // Newer IndexInfo uses mangled name of expression instead of column ID of the table
        // that was indexed.
        target += qlexpr::YcqlName::DemangleName(index_info.columns()[i].column_name);
      } else {
        target += ColumnName(indexed_schema, index_info.columns()[i].indexed_column_id);
      }
    }

    string include;
    for (size_t i = index_info.hash_column_count() + index_info.range_column_count();
         i < index_info.columns().size(); i++) {
      if (index_info.use_mangled_column_name()) {
        // Newer IndexInfo uses mangled name of expression instead of column ID of the table
        // that was indexed.
        include += qlexpr::YcqlName::DemangleName(index_info.columns()[i].column_name);
      } else {
        include += ColumnName(indexed_schema, index_info.columns()[i].indexed_column_id);
      }
      if (i != index_info.columns().size() - 1) {
        include += ", ";
      }
    }

    string predicate;
    if (index_info.where_predicate_spec()) {
      predicate = QLExpressionPBToPredicateString(index_info.where_predicate_spec()->where_expr(),
                                                  indexed_schema);
    }

    QLValue options;
    options.set_map_value();
    options.add_map_key()->set_string_value("target");
    options.add_map_value()->set_string_value(target);
    if (!include.empty()) {
      options.add_map_key()->set_string_value("include");
      options.add_map_value()->set_string_value(include);
    }
    if (!predicate.empty()) {
      options.add_map_key()->set_string_value("predicate");
      options.add_map_value()->set_string_value(predicate);
    }
    RETURN_NOT_OK(SetColumnValue(kOptions, options.value(), &row));

    // Create appropriate table uuids.
    // Note: table id is in host byte order.
    auto uuid = VERIFY_RESULT(Uuid::FromHexString(indexed_table_id));
    RETURN_NOT_OK(SetColumnValue(kTableId, uuid, &row));
    uuid = VERIFY_RESULT(Uuid::FromHexString(table->id()));
    RETURN_NOT_OK(SetColumnValue(kIndexId, uuid, &row));

    Schema schema;
    RETURN_NOT_OK(table->GetSchema(&schema));
    const auto & table_properties = schema.table_properties();

    if (table_properties.HasNumTablets()) {
      int32_t num_tablets = table_properties.num_tablets();
      RETURN_NOT_OK(SetColumnValue(kNumTablets, num_tablets, &row));
    }

    QLValue txn;
    txn.set_map_value();
    txn.add_map_key()->set_string_value("enabled");
    txn.add_map_value()->set_string_value(table_properties.is_transactional() ? "true" : "false");
    if (table_properties.consistency_level() == YBConsistencyLevel::USER_ENFORCED) {
      // If consistency level is user-encorced, show it also. Omit the other consistency levels
      // which are not recognized by "CREATE INDEX" syntax.
      txn.add_map_key()->set_string_value("consistency_level");
      txn.add_map_value()->set_string_value("user_enforced");
    }
    RETURN_NOT_OK(SetColumnValue(kTransactions, txn.value(), &row));

    RETURN_NOT_OK(SetColumnValue(kIsUnique, table->is_unique_index(), &row));
  }

  return vtable;
}

Schema YQLIndexesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kIndexName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kKind, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kOptions,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kTableId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kIndexId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kTransactions,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kIsUnique, QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn(kNumTablets, QLType::Create(DataType::INT32)));

  return builder.Build();
}

}  // namespace master
}  // namespace yb
