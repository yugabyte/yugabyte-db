//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// SQL Option Declaration.
// This module defines the enums to represent various optional clause in SQL.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_OPTION_H_
#define YB_SQL_PTREE_PT_OPTION_H_

namespace yb {
namespace sql {

enum class PTOptionExist : int {
  DEFAULT = 0,
  IF_EXISTS,
  IF_NOT_EXISTS,
};

}  // namespace sql
}  // namespace yb

#endif // YB_SQL_PTREE_PT_OPTION_H_
