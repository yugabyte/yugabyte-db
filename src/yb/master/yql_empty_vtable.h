// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_EMPTY_VTABLE_H
#define YB_MASTER_YQL_EMPTY_VTABLE_H

#include "yb/common/schema.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// Generic virtual table which we currently use for system tables that are empty. Although we are
// not sure when the class will be deleted since currently it does not look like we need to populate
// those tables.
class YQLEmptyVTable : public YQLVirtualTable {
 public:
  explicit YQLEmptyVTable(const std::string& table_name);
  CHECKED_STATUS RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const override;
 protected:
  Schema CreateSchema(const std::string& table_name) const override;
 private:
  // system_schema.aggregates.
  CHECKED_STATUS CreateAggregatesSchema(Schema* schema) const;

  // system_schema.columns.
  CHECKED_STATUS CreateColumnsSchema(Schema* schema) const;

  // system_schema.functions.
  CHECKED_STATUS CreateFunctionsSchema(Schema* schema) const;

  // system_schema.indexes.
  CHECKED_STATUS CreateIndexesSchema(Schema* schema) const;

  // system_schema.triggers.
  CHECKED_STATUS CreateTriggersSchema(Schema* schema) const;

  // system_schema.types.
  CHECKED_STATUS CreateTypesSchema(Schema* schema) const;

  // system_schema.views.
  CHECKED_STATUS CreateViewsSchema(Schema* schema) const;
};

}  // namespace master
}  // namespace yb

#endif // YB_MASTER_YQL_EMPTY_VTABLE_H
