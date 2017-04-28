// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_VIEWS_VTABLE_H
#define YB_MASTER_YQL_VIEWS_VTABLE_H

#include "yb/master/yql_empty_vtable.h"

namespace yb {
namespace master {

// VTable implementation of system_schema.views.
class YQLViewsVTable : public YQLEmptyVTable {
 public:
  explicit YQLViewsVTable(const Master* const master);
 protected:
  Schema CreateSchema() const;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_VIEWS_VTABLE_H
