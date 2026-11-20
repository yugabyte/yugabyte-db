//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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
// Unit tests for the LIST ROLES / LIST PERMISSIONS semantics in exec/list_roles_permissions.h.
// These need no cluster. The fixture mirrors the one used to record Apache Cassandra 3.11.19's
// behavior, and the expected results below are Cassandra's output for the same statements.
//--------------------------------------------------------------------------------------------------

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

#include "yb/yql/cql/ql/exec/list_roles_permissions.h"
#include "yb/yql/cql/ql/util/errcodes.h"

namespace yb {
namespace ql {

using std::string;
using std::vector;

namespace {

constexpr auto kAll = PermissionType::ALL_PERMISSION;

// Fixture (same as the Cassandra 3.11.19 run):
//   alice (login) <- parent <- grandparent; other (login) is unrelated; descr (login) has DESCRIBE
//   ON ALL ROLES; cassandra is the superuser.
//   alice: MODIFY <table ks.t>; parent: SELECT <keyspace ks>;
//   grandparent: SELECT <all keyspaces>, ALTER <role other>; other: SELECT <table ks.t>.
AuthCatalog MakeCatalog() {
  AuthCatalog catalog;
  catalog.AddRole({"cassandra", true, true, {}});
  catalog.AddRole({"alice", true, false, {"parent"}});
  catalog.AddRole({"parent", false, false, {"grandparent"}});
  catalog.AddRole({"grandparent", false, false, {}});
  catalog.AddRole({"other", true, false, {}});
  catalog.AddRole({"descr", true, false, {}});
  CHECK_OK(catalog.AddGrant("alice", "data/ks/t", {"MODIFY"}));
  CHECK_OK(catalog.AddGrant("parent", "data/ks", {"SELECT"}));
  CHECK_OK(catalog.AddGrant("grandparent", "data", {"SELECT"}));
  CHECK_OK(catalog.AddGrant("grandparent", "roles/other", {"ALTER"}));
  CHECK_OK(catalog.AddGrant("other", "data/ks/t", {"SELECT"}));
  CHECK_OK(catalog.AddGrant("descr", "roles", {"DESCRIBE"}));
  return catalog;
}

vector<string> RoleNames(const Result<vector<AuthRoleInfo>>& roles) {
  CHECK_OK(roles);
  vector<string> names;
  for (const auto& role : *roles) {
    names.push_back(role.role);
  }
  return names;
}

// Formats rows as "role|resource display name|permission".
vector<string> Rows(const Result<vector<PermissionRow>>& rows) {
  CHECK_OK(rows);
  vector<string> result;
  for (const auto& row : *rows) {
    result.push_back(row.role + "|" + *ResourceDisplayName(row.resource) + "|" +
                     ListPermissionName(row.permission));
  }
  return result;
}

std::optional<ListResourceSpec> On(ResourceType type, const string& canonical_name,
                                   const string& keyspace = string()) {
  return ListResourceSpec{type, canonical_name, keyspace};
}

template <class T>
void ExpectError(const Result<T>& result, ErrorCode code, const string& message) {
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(code, GetErrorCode(result.status())) << result.status();
  EXPECT_EQ(message, result.status().message().ToBuffer());
}

}  // namespace

class ListRolesPermissionsTest : public YBTest {
 protected:
  const AuthCatalog catalog_ = MakeCatalog();
};

//--------------------------------------------------------------------------------------------------
// Catalog helpers.

TEST_F(ListRolesPermissionsTest, RolesOf) {
  EXPECT_EQ((std::set<string>{"alice", "grandparent", "parent"}),
            catalog_.RolesOf("alice", true /* recursive */));
  EXPECT_EQ((std::set<string>{"alice", "parent"}), catalog_.RolesOf("alice", false));
  EXPECT_EQ((std::set<string>{"grandparent"}), catalog_.RolesOf("grandparent", true));
  EXPECT_TRUE(catalog_.RolesOf("ghost", true).empty());
}

TEST_F(ListRolesPermissionsTest, RolesOfToleratesCyclesAndMissingRoles) {
  AuthCatalog catalog;
  catalog.AddRole({"a", false, false, {"b"}});
  catalog.AddRole({"b", false, false, {"a", "dropped"}});
  EXPECT_EQ((std::set<string>{"a", "b"}), catalog.RolesOf("a", true));
}

TEST_F(ListRolesPermissionsTest, SuperuserAndPermissionsAreInherited) {
  AuthCatalog catalog = MakeCatalog();
  catalog.AddRole({"admin_group", false, true, {}});
  catalog.AddRole({"bob", true, false, {"admin_group"}});
  EXPECT_TRUE(catalog.IsSuperuser("cassandra"));
  EXPECT_TRUE(catalog.IsSuperuser("bob"));
  EXPECT_FALSE(catalog.IsSuperuser("alice"));

  EXPECT_TRUE(catalog.HasPermission("alice", "data", PermissionType::SELECT_PERMISSION));
  EXPECT_FALSE(catalog.HasPermission("alice", "data", PermissionType::MODIFY_PERMISSION));
  EXPECT_FALSE(catalog.HasPermission("other", "data", PermissionType::SELECT_PERMISSION));
}

TEST_F(ListRolesPermissionsTest, UnknownGrantedPermissionIsAnError) {
  AuthCatalog catalog;
  catalog.AddRole({"r", false, false, {}});
  ASSERT_NOK(catalog.AddGrant("r", "data", {"EXECUTE"}));
}

//--------------------------------------------------------------------------------------------------
// Resource names.

TEST_F(ListRolesPermissionsTest, ResourceDisplayName) {
  EXPECT_EQ("<all keyspaces>", *ResourceDisplayName("data"));
  EXPECT_EQ("<keyspace ks>", *ResourceDisplayName("data/ks"));
  EXPECT_EQ("<table ks.t>", *ResourceDisplayName("data/ks/t"));
  EXPECT_EQ("<all roles>", *ResourceDisplayName("roles"));
  EXPECT_EQ("<role r>", *ResourceDisplayName("roles/r"));
  // Role names may contain '/'.
  EXPECT_EQ("<role a/b>", *ResourceDisplayName("roles/a/b"));
  EXPECT_FALSE(ResourceDisplayName("functions/ks").has_value());
  // With the ON resource, a keyspace name that contains '/' is shown correctly.
  const auto on_table = On(ResourceType::TABLE, "data/a/b/t", "a/b");
  EXPECT_EQ("<table a/b.t>", *ResourceDisplayName("data/a/b/t", on_table));
  EXPECT_EQ("<keyspace a/b>", *ResourceDisplayName("data/a/b", on_table));
  EXPECT_EQ("<all keyspaces>", *ResourceDisplayName("data", on_table));
  EXPECT_EQ("<keyspace a/b>",
            *ResourceDisplayName("data/a/b", On(ResourceType::KEYSPACE, "data/a/b", "a/b")));
}

// A quoted keyspace name may contain '/'. LIST ... ON TABLE must not treat an unrelated keyspace
// that is a prefix of it ("a" for "a/b") as the table's parent.
TEST_F(ListRolesPermissionsTest, ListPermissionsOnTableInKeyspaceWithSlash) {
  AuthCatalog catalog = MakeCatalog();
  catalog.AddRole({"slash", false, false, {}});
  ASSERT_OK(catalog.AddGrant("slash", "data/a/b/t", {"SELECT"}));
  ASSERT_OK(catalog.AddGrant("slash", "data/a/b", {"MODIFY"}));
  ASSERT_OK(catalog.AddGrant("slash", "data/a", {"ALTER"}));
  const auto on_table = On(ResourceType::TABLE, "data/a/b/t", "a/b");
  const auto rows = ListPermissions(catalog, "cassandra", kAll, on_table, "slash", true);
  ASSERT_OK(rows);
  vector<string> result;
  for (const auto& row : *rows) {
    result.push_back(row.role + "|" + *ResourceDisplayName(row.resource, on_table) + "|" +
                     ListPermissionName(row.permission));
  }
  EXPECT_EQ((vector<string>{"slash|<keyspace a/b>|MODIFY", "slash|<table a/b.t>|SELECT"}),
            result);
}

TEST_F(ListRolesPermissionsTest, ResourceChain) {
  EXPECT_EQ((vector<string>{"data/ks/t", "data/ks", "data"}),
            ResourceChain(*On(ResourceType::TABLE, "data/ks/t", "ks")));
  EXPECT_EQ((vector<string>{"data/ks", "data"}),
            ResourceChain(*On(ResourceType::KEYSPACE, "data/ks", "ks")));
  EXPECT_EQ((vector<string>{"data"}), ResourceChain(*On(ResourceType::ALL_KEYSPACES, "data")));
  EXPECT_EQ((vector<string>{"roles/r", "roles"}),
            ResourceChain(*On(ResourceType::ROLE, "roles/r")));
  EXPECT_EQ((vector<string>{"roles"}), ResourceChain(*On(ResourceType::ALL_ROLES, "roles")));
  // The keyspace parent comes from the statement, not from splitting the canonical name.
  EXPECT_EQ((vector<string>{"data/a/b/t", "data/a/b", "data"}),
            ResourceChain(*On(ResourceType::TABLE, "data/a/b/t", "a/b")));
}

//--------------------------------------------------------------------------------------------------
// LIST ROLES.

TEST_F(ListRolesPermissionsTest, ListRolesAsSuperuser) {
  EXPECT_EQ((vector<string>{"alice", "cassandra", "descr", "grandparent", "other", "parent"}),
            RoleNames(ListRoles(catalog_, "cassandra", std::nullopt, true)));
  EXPECT_EQ((vector<string>{"alice", "grandparent", "parent"}),
            RoleNames(ListRoles(catalog_, "cassandra", "alice", true)));
  EXPECT_EQ((vector<string>{"alice", "parent"}),
            RoleNames(ListRoles(catalog_, "cassandra", "alice", false)));
}

TEST_F(ListRolesPermissionsTest, ListRolesWithDescribe) {
  EXPECT_EQ((vector<string>{"alice", "cassandra", "descr", "grandparent", "other", "parent"}),
            RoleNames(ListRoles(catalog_, "descr", std::nullopt, true)));
  EXPECT_EQ((vector<string>{"other"}), RoleNames(ListRoles(catalog_, "descr", "other", true)));
}

TEST_F(ListRolesPermissionsTest, ListRolesWithoutDescribe) {
  // Falls back to the caller's own roles instead of failing.
  EXPECT_EQ((vector<string>{"alice", "grandparent", "parent"}),
            RoleNames(ListRoles(catalog_, "alice", std::nullopt, true)));
  EXPECT_EQ((vector<string>{"alice", "parent"}),
            RoleNames(ListRoles(catalog_, "alice", std::nullopt, false)));
  EXPECT_EQ((vector<string>{"alice", "grandparent", "parent"}),
            RoleNames(ListRoles(catalog_, "alice", "alice", true)));
  EXPECT_EQ((vector<string>{"grandparent", "parent"}),
            RoleNames(ListRoles(catalog_, "alice", "parent", true)));
  EXPECT_EQ((vector<string>{"grandparent"}),
            RoleNames(ListRoles(catalog_, "alice", "grandparent", true)));
  ExpectError(ListRoles(catalog_, "alice", "other", true), ErrorCode::UNAUTHORIZED,
              "You are not authorized to view roles granted to other");
}

TEST_F(ListRolesPermissionsTest, ListRolesOfMissingRoleIsCheckedBeforeAuthorization) {
  ExpectError(ListRoles(catalog_, "alice", "ghost", true), ErrorCode::ROLE_NOT_FOUND,
              "<role ghost> doesn't exist");
  ExpectError(ListRoles(catalog_, "cassandra", "ghost", true), ErrorCode::ROLE_NOT_FOUND,
              "<role ghost> doesn't exist");
}

TEST_F(ListRolesPermissionsTest, ListRolesReturnsRoleAttributes) {
  const auto roles = ListRoles(catalog_, "cassandra", std::nullopt, true);
  ASSERT_OK(roles);
  for (const auto& role : *roles) {
    if (role.role == "cassandra") {
      EXPECT_TRUE(role.is_superuser);
      EXPECT_TRUE(role.can_login);
    } else if (role.role == "parent") {
      EXPECT_FALSE(role.is_superuser);
      EXPECT_FALSE(role.can_login);
    }
  }
}

//--------------------------------------------------------------------------------------------------
// LIST PERMISSIONS.

TEST_F(ListRolesPermissionsTest, ListPermissionsIncludesInheritedGrantsWithHolderRole) {
  const vector<string> expected = {
      "alice|<table ks.t>|MODIFY",
      "grandparent|<all keyspaces>|SELECT",
      "grandparent|<role other>|ALTER",
      "parent|<keyspace ks>|SELECT",
  };
  EXPECT_EQ(expected, Rows(ListPermissions(catalog_, "alice", kAll, std::nullopt, "alice", true)));
  // NORECURSIVE does not drop inherited grants.
  EXPECT_EQ(expected,
            Rows(ListPermissions(catalog_, "alice", kAll, std::nullopt, "alice", false)));
  EXPECT_EQ((vector<string>{"grandparent|<all keyspaces>|SELECT",
                            "grandparent|<role other>|ALTER",
                            "parent|<keyspace ks>|SELECT"}),
            Rows(ListPermissions(catalog_, "alice", kAll, std::nullopt, "parent", true)));
}

TEST_F(ListRolesPermissionsTest, ListPermissionsOnTableIncludesParentResources) {
  EXPECT_EQ((vector<string>{"alice|<table ks.t>|MODIFY",
                            "grandparent|<all keyspaces>|SELECT",
                            "parent|<keyspace ks>|SELECT"}),
            Rows(ListPermissions(catalog_, "alice", kAll,
                                 On(ResourceType::TABLE, "data/ks/t", "ks"), "alice", true)));
  // The permission filter applies.
  EXPECT_EQ((vector<string>{"grandparent|<all keyspaces>|SELECT",
                            "parent|<keyspace ks>|SELECT"}),
            Rows(ListPermissions(catalog_, "alice", PermissionType::SELECT_PERMISSION,
                                 On(ResourceType::TABLE, "data/ks/t", "ks"), "alice", true)));
}

TEST_F(ListRolesPermissionsTest, ListPermissionsNoRecursiveDropsParentResources) {
  EXPECT_EQ((vector<string>{"parent|<keyspace ks>|SELECT"}),
            Rows(ListPermissions(catalog_, "cassandra", kAll,
                                 On(ResourceType::KEYSPACE, "data/ks", "ks"), "alice", false)));
  EXPECT_EQ((vector<string>{"alice|<table ks.t>|MODIFY"}),
            Rows(ListPermissions(catalog_, "cassandra", kAll,
                                 On(ResourceType::TABLE, "data/ks/t", "ks"), "alice", false)));
}

TEST_F(ListRolesPermissionsTest, ListPermissionsWithoutOfListsAllRoles) {
  EXPECT_EQ((vector<string>{"alice|<table ks.t>|MODIFY",
                            "grandparent|<all keyspaces>|SELECT",
                            "other|<table ks.t>|SELECT",
                            "parent|<keyspace ks>|SELECT"}),
            Rows(ListPermissions(catalog_, "descr", kAll,
                                 On(ResourceType::TABLE, "data/ks/t", "ks"), std::nullopt, true)));
}

TEST_F(ListRolesPermissionsTest, ListPermissionsOnRoleIncludesAllRoles) {
  // A permission that does not apply to the resource type is not an error: DESCRIBE is granted on
  // the parent <all roles>.
  EXPECT_EQ((vector<string>{"descr|<all roles>|DESCRIBE"}),
            Rows(ListPermissions(catalog_, "cassandra", PermissionType::DESCRIBE_PERMISSION,
                                 On(ResourceType::ROLE, "roles/other"), std::nullopt, true)));
  EXPECT_EQ((vector<string>{"descr|<all roles>|DESCRIBE", "grandparent|<role other>|ALTER"}),
            Rows(ListPermissions(catalog_, "cassandra", kAll,
                                 On(ResourceType::ROLE, "roles/other"), std::nullopt, true)));
}

TEST_F(ListRolesPermissionsTest, ListPermissionsEmptyResult) {
  EXPECT_TRUE(Rows(ListPermissions(catalog_, "cassandra", PermissionType::MODIFY_PERMISSION,
                                   On(ResourceType::ALL_ROLES, "roles"), std::nullopt, true))
                  .empty());
}

TEST_F(ListRolesPermissionsTest, ListPermissionsUsesCassandraPermissionOrder) {
  AuthCatalog catalog = MakeCatalog();
  catalog.AddRole({"ordered", false, false, {}});
  ASSERT_OK(catalog.AddGrant("ordered", "data/ks",
                             {"SELECT", "AUTHORIZE", "MODIFY", "DROP", "ALTER", "CREATE"}));
  EXPECT_EQ((vector<string>{"ordered|<keyspace ks>|CREATE",
                            "ordered|<keyspace ks>|ALTER",
                            "ordered|<keyspace ks>|DROP",
                            "ordered|<keyspace ks>|SELECT",
                            "ordered|<keyspace ks>|MODIFY",
                            "ordered|<keyspace ks>|AUTHORIZE"}),
            Rows(ListPermissions(catalog, "cassandra", kAll, std::nullopt, "ordered", true)));
}

TEST_F(ListRolesPermissionsTest, ListPermissionsAuthorization) {
  ExpectError(ListPermissions(catalog_, "alice", kAll, std::nullopt, std::nullopt, true),
              ErrorCode::UNAUTHORIZED, "You are not authorized to view everyone's permissions");
  ExpectError(ListPermissions(catalog_, "alice", kAll, On(ResourceType::TABLE, "data/ks/t", "ks"),
                              std::nullopt, true),
              ErrorCode::UNAUTHORIZED, "You are not authorized to view everyone's permissions");
  ExpectError(ListPermissions(catalog_, "alice", kAll, std::nullopt, "other", true),
              ErrorCode::UNAUTHORIZED, "You are not authorized to view other's permissions");
  // Roles granted to the caller are visible, as are the caller's own.
  ASSERT_OK(ListPermissions(catalog_, "alice", kAll, std::nullopt, "grandparent", true));
  // DESCRIBE on ALL ROLES and superusers see everyone.
  ASSERT_OK(ListPermissions(catalog_, "descr", kAll, std::nullopt, "other", true));
  ASSERT_OK(ListPermissions(catalog_, "cassandra", kAll, std::nullopt, std::nullopt, true));
}

// Verified against Cassandra 3.11.19: DESCRIBE on a single role (for example the creator's grant)
// allows LIST PERMISSIONS OF that role, with the grants it inherits, but not of its parent roles,
// not of everyone, and not LIST ROLES OF that role.
TEST_F(ListRolesPermissionsTest, DescribeOnOneRoleAllowsListPermissionsOfThatRole) {
  AuthCatalog catalog = MakeCatalog();
  catalog.AddRole({"creator", true, false, {"creator_group"}});
  catalog.AddRole({"creator_group", false, false, {}});
  catalog.AddRole({"created", false, false, {"created_parent"}});
  catalog.AddRole({"created_parent", false, false, {}});
  ASSERT_OK(catalog.AddGrant("creator_group", "roles/created", {"DESCRIBE"}));
  ASSERT_OK(catalog.AddGrant("created", "data/ks", {"SELECT"}));
  ASSERT_OK(catalog.AddGrant("created_parent", "data/ks", {"MODIFY"}));

  EXPECT_EQ((vector<string>{"created|<keyspace ks>|SELECT",
                            "created_parent|<keyspace ks>|MODIFY"}),
            Rows(ListPermissions(catalog, "creator", kAll, std::nullopt, "created", true)));
  ExpectError(ListPermissions(catalog, "creator", kAll, std::nullopt, "created_parent", true),
              ErrorCode::UNAUTHORIZED,
              "You are not authorized to view created_parent's permissions");
  ExpectError(ListPermissions(catalog, "creator", kAll, std::nullopt, std::nullopt, true),
              ErrorCode::UNAUTHORIZED, "You are not authorized to view everyone's permissions");
  ExpectError(ListRoles(catalog, "creator", "created", true), ErrorCode::UNAUTHORIZED,
              "You are not authorized to view roles granted to created");
  // Another permission on the role is not enough.
  ExpectError(ListPermissions(catalog_, "grandparent", kAll, std::nullopt, "other", true),
              ErrorCode::UNAUTHORIZED, "You are not authorized to view other's permissions");
}

TEST_F(ListRolesPermissionsTest, ListPermissionsMissingRolesAreCheckedBeforeAuthorization) {
  ExpectError(ListPermissions(catalog_, "alice", kAll, std::nullopt, "ghost", true),
              ErrorCode::ROLE_NOT_FOUND, "<role ghost> doesn't exist");
  ExpectError(ListPermissions(catalog_, "alice", kAll, On(ResourceType::ROLE, "roles/ghost"),
                              "alice", true),
              ErrorCode::ROLE_NOT_FOUND, "<role ghost> doesn't exist");
}

}  // namespace ql
}  // namespace yb
