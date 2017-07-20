// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_AUTH_ROLES_VTABLE_H
#define YB_MASTER_YQL_AUTH_ROLES_VTABLE_H

#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// VTable implementation of system_auth.roles.
class YQLAuthRolesVTable : public YQLVirtualTable {
 public:
  explicit YQLAuthRolesVTable(const Master* const master);

  CHECKED_STATUS RetrieveData(const YQLReadRequestPB& request,
                              std::unique_ptr<YQLRowBlock>* vtable) const;

 protected:
  Schema CreateSchema() const;

 private:
  static constexpr const char* const kRole = "role";
  static constexpr const char* const kCanLogin = "can_login";
  static constexpr const char* const kIsSuperuser = "is_superuser";
  static constexpr const char* const kMemberOf = "member_of";
  static constexpr const char* const kSaltedHash = "salted_hash";
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_AUTH_ROLES_VTABLE_H
