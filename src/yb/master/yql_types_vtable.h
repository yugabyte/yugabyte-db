// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_TYPES_VTABLE_H
#define YB_MASTER_YQL_TYPES_VTABLE_H

#include "yb/master/yql_empty_vtable.h"

namespace yb {
namespace master {

// VTable implementation of system_schema.types.
class YQLTypesVTable : public YQLVirtualTable {
 public:
  explicit YQLTypesVTable(const Master* const master);
  CHECKED_STATUS RetrieveData(const YQLReadRequestPB& request,
                              std::unique_ptr<YQLRowBlock>* vtable) const;
 protected:
  Schema CreateSchema() const;
 private:
  static constexpr const char* const kKeyspaceName = "keyspace_name";
  static constexpr const char* const kTypeName = "type_name";
  static constexpr const char* const kFieldNames = "field_names";
  static constexpr const char* const kFieldTypes = "field_types";

};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_TYPES_VTABLE_H
