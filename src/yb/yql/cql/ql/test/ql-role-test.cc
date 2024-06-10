//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/util/flags.h"
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/permissions.h"

#include "yb/common/ql_value.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/mini_master.h"

#include "yb/util/crypt.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"

DECLARE_bool(use_cassandra_authentication);
DECLARE_bool(ycql_allow_non_authenticated_password_reset);

constexpr const char* const kDefaultCassandraUsername = "cassandra";

namespace yb {
namespace master {
class CatalogManager;
class Master;
}
namespace ql {

using yb::util::kBcryptHashSize;
using yb::util::bcrypt_hashpw;
using yb::util::bcrypt_checkpw;
using strings::Substitute;
using std::string;

#define EXEC_DUPLICATE_CREATE_ROLE_STMT(stmt)                           \
  EXEC_INVALID_STMT_WITH_ERROR(stmt, "Duplicate Role. ")

static const char invalid_grant_describe_error_msg[] =
    "Resource type DataResource does not support any of the requested permissions";
static const std::vector<string> all_permissions =
    {"ALTER", "AUTHORIZE", "CREATE", "DESCRIBE", "DROP", "MODIFY", "SELECT"};
static const std::vector<string> all_permissions_minus_describe =
    {"ALTER", "AUTHORIZE", "CREATE", "DROP", "MODIFY", "SELECT"};
static const std::vector<string> all_permissions_for_all_roles =
    {"ALTER", "AUTHORIZE", "CREATE", "DESCRIBE", "DROP"};
static const std::vector<string> all_permissions_for_keyspace =
    {"ALTER", "AUTHORIZE", "CREATE", "DROP", "MODIFY", "SELECT"};
static const std::vector<string> all_permissions_for_table =
    {"ALTER", "AUTHORIZE", "DROP", "MODIFY", "SELECT"};
static const std::vector<string> all_permissions_for_role =
    {"ALTER", "AUTHORIZE", "DROP"};

class QLTestAuthentication : public QLTestBase {
 public:
  QLTestAuthentication() : QLTestBase(), permissions_cache_(client_.get(), false) {}

  virtual void SetUp() override {
    QLTestBase::SetUp();
  }

  virtual void TearDown() override {
    QLTestBase::TearDown();
  }

  uint64_t GetPermissionsVersion() {
    CHECK_OK(client_->GetPermissions(&permissions_cache_));
    boost::optional<uint64_t> version = permissions_cache_.version();
    CHECK(version);
    return *version;
  }

  // Used to execute any statement that can modify a role (add/remove/update) or a permission.
  // It automatically verifies that the roles-version in the master equals to what we expect.
  void ExecuteValidModificationStmt(TestQLProcessor* processor, const string& stmt) {
    ASSERT_OK(processor->Run(stmt));
    version_++;
    if (stmt.substr(0, 4) == "DROP" || stmt.substr(0, 6) == "CREATE") {
      // These statements increment the role version twice, because they either grant or revoke
      // permissions.
      version_++;
    }
    ASSERT_EQ(GetPermissionsVersion(), version_);
  }

  inline const string CreateStmt(string params) {
    return "CREATE ROLE " + params;
  }

  inline const string CreateIfNotExistsStmt(string params) {
    return "CREATE ROLE IF NOT EXISTS " + params;
  }

  inline const string DropStmt(string params) {
    return "DROP ROLE " + params;
  }

  inline const string DropIfExistsStmt(string params) {
    return "DROP ROLE IF EXISTS " + params;
  }

  inline const string AlterStmt(const string& role, string password) {
    return Substitute("ALTER ROLE $0 WITH PASSWORD = '$1'", role, password);
  }

  static const string GrantStmt(const string& role, const string& recipient) {
    return Substitute("GRANT $0 TO $1", role, recipient);
  }

  static const string RevokeStmt(const string& role, const string& recipient) {
    return Substitute("REVOKE $0 FROM $1", role, recipient);
  }

  // Use superuser = false if there is a need to revoke permissions.
  void CreateRole(TestQLProcessor* processor, const string& role_name, bool superuser = false) {
    const string create_stmt = Substitute(
        "CREATE ROLE $0 WITH LOGIN = TRUE AND SUPERUSER = $1 AND PASSWORD = 'TEST';", role_name,
            superuser ? "TRUE" : "FALSE");
    ExecuteValidModificationStmt(processor, create_stmt);
  }

  void GrantRole(TestQLProcessor* processor, const string& role, const string& recipient) {
    const string grant_stmt = GrantStmt(role, recipient);
    ExecuteValidModificationStmt(processor, grant_stmt);
  }

  // Executes a revoke role command that fails silently.
  void RevokeRoleIgnored(TestQLProcessor* processor, const string& role, const string& recipient) {
    const string revoke_stmt = RevokeStmt(role, recipient);
    ASSERT_OK(processor->Run(revoke_stmt));
  }

  void RevokeRole(TestQLProcessor* processor, const string& role, const string& recipient) {
    const string revoke_stmt = RevokeStmt(role, recipient);
    ExecuteValidModificationStmt(processor, revoke_stmt);
  }

  string SelectStmt(const string& role_name) const {
    return Substitute("SELECT * FROM system_auth.roles where role='$0';", role_name);
  }

 protected:
  uint64_t version_ = 0;

 private:
  client::internal::PermissionsCache permissions_cache_;
};

class TestQLPermission : public QLTestAuthentication {
 public:
  TestQLPermission() : QLTestAuthentication() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = true;
  }

  // Helper Functions
  void CreateTable(TestQLProcessor* processor, const string& keyspace_name,
                   const string& table_name) {
    const string create_stmt = Substitute(
        "CREATE TABLE $0.$1(id int, name varchar, primary key(id));", keyspace_name, table_name);
    Status s = processor->Run(create_stmt);
    CHECK(s.ok());
    // Creating a table grants all the permissions on the new table to the creator role, and this
    // increments role's version.
    version_++;
    ASSERT_EQ(GetPermissionsVersion(), version_);
  }

  void CreateKeyspace(TestQLProcessor* processor, const string& keyspace_name) {
    const string create_stmt = Substitute(
        "CREATE KEYSPACE $0;", keyspace_name);
    Status s = processor->Run(create_stmt);
    CHECK(s.ok());
    // Creating a keyspace grants all the permissions on the new keyspace to the creator role, and
    // this increments role's version.
    version_++;
    ASSERT_EQ(GetPermissionsVersion(), version_);
  }

  string GrantAllKeyspaces(const string& permission, const string& role_name) const {
    return Substitute("GRANT $0 ON ALL KEYSPACES TO $1;", permission, role_name);
  }

  string RevokeAllKeyspaces(const string& permission, const string& role_name) const {
    return Substitute("REVOKE $0 ON ALL KEYSPACES FROM $1;", permission, role_name);
  }

  string GrantKeyspace(const string& permission, const string& keyspace,
                            const string& role_name) const {
    return Substitute("GRANT $0 ON KEYSPACE $1 TO $2;", permission, keyspace, role_name);
  }

  string RevokeKeyspace(const string& permission, const string& keyspace,
                       const string& role_name) const {
    return Substitute("REVOKE $0 ON KEYSPACE $1 FROM $2;", permission, keyspace, role_name);
  }

  string GrantTable(const string& permission, const string& table, const string& role_name) const {
    return Substitute("GRANT $0 ON TABLE $1 TO $2;", permission, table, role_name);
  }

  string RevokeTable(const string& permission, const string& table, const string& role_name) const {
    return Substitute("REVOKE $0 ON TABLE $1 FROM $2;", permission, table, role_name);
  }

  string GrantAllRoles(const string& permission, const string& role_name) const {
    return Substitute("GRANT $0 ON ALL ROLES TO $1;", permission, role_name);
  }

  string RevokeAllRoles(const string& permission, const string& role_name) const {
    return Substitute("REVOKE $0 ON ALL ROLES FROM $1;", permission, role_name);
  }

  string GrantRole(const string& permission, const string& role_resource,
                   const string& role_name) const {
    return Substitute("GRANT $0 ON ROLE $1 TO $2;", permission, role_resource, role_name);
  }

  string SelectStmt(const string& role_name) const {
    return Substitute("SELECT * FROM system_auth.role_permissions where role='$0';", role_name);
  }

  string SelectStmt(const string& role_name, const string& resource) const {
    return Substitute(
        "SELECT * FROM system_auth.role_permissions where role='$0' AND resource='$1';",
        role_name, resource);
  }

  void CheckRowContents(const qlexpr::QLRow& row, const string& canonical_resource,
                        const std::vector<string> &permissions, const string& role_name) {
    EXPECT_EQ(role_name, row.column(0).string_value());
    EXPECT_EQ(canonical_resource, row.column(1).string_value());

    EXPECT_EQ(InternalType::kListValue, row.column(2).type());
    const QLSeqValuePB& list_value = row.column(2).list_value();
    EXPECT_EQ(permissions.size(), list_value.elems_size());
    // Create a set of the values:
    std::unordered_set<string> permissions_set;
    for (const auto& elem : list_value.elems()) {
      permissions_set.insert(elem.string_value());
    }

    for (const auto& permission : permissions) {
      EXPECT_TRUE(permissions_set.find(permission) != permissions_set.end());
    }
  }

  // Issues a GRANT or REVOKE PERMISSION statement and verifies that it was granted/revoked
  // correctly.
  void GrantRevokePermissionAndVerify(TestQLProcessor* processor, const string& stmt,
                                      const string& canonical_resource,
                                      const std::vector<string>& permissions,
                                      const RoleName& role_name) {

    LOG (INFO) << "Running statement " << stmt;
    ExecuteValidModificationStmt(processor, stmt);

    auto select = SelectStmt(role_name, canonical_resource);
    auto s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();

    if (permissions.empty()) {
      EXPECT_EQ(0, row_block->row_count());
      return;
    }

    EXPECT_EQ(1, row_block->row_count());

    auto& row = row_block->row(0);
    CheckRowContents(row, canonical_resource, permissions, role_name);

    std::unordered_map<std::string, uint64_t>  permission_map = {
        {"ALTER", PermissionType::ALTER_PERMISSION},
        {"CREATE", PermissionType::CREATE_PERMISSION},
        {"DROP", PermissionType::DROP_PERMISSION },
        {"SELECT", PermissionType::SELECT_PERMISSION},
        {"MODIFY", PermissionType::MODIFY_PERMISSION},
        {"AUTHORIZE", PermissionType::AUTHORIZE_PERMISSION},
        {"DESCRIBE", PermissionType::DESCRIBE_PERMISSION}
    };

    client::internal::PermissionsCache permissions_cache(client_.get(), false);
    ASSERT_OK(client_->GetPermissions(&permissions_cache));

    std::shared_ptr<client::internal::RolesPermissionsMap> roles_permissions_map =
        permissions_cache.get_roles_permissions_map();

    const auto& role_permissions_itr = roles_permissions_map->find(role_name);

    // Verify that the role exists in the cache.
    ASSERT_NE(role_permissions_itr, roles_permissions_map->end());

    const auto& role_permissions = role_permissions_itr->second;

    Permissions cached_permissions_bitset;
    if (canonical_resource == kRolesDataResource) {
      cached_permissions_bitset = role_permissions.all_keyspaces_permissions();
    } else if (canonical_resource == kRolesRoleResource) {
      cached_permissions_bitset = role_permissions.all_roles_permissions();
    } else {
      // Assert that the canonical resource exists in the cache.
      const auto& canonical_resource_itr =
          role_permissions.resource_permissions().find(canonical_resource);
      ASSERT_NE(canonical_resource_itr, role_permissions.resource_permissions().end());

      cached_permissions_bitset = canonical_resource_itr->second;
    }

    ASSERT_EQ(cached_permissions_bitset.count(), permissions.size());
    for (const string& expected_permission : permissions) {
      CHECK(cached_permissions_bitset.test(permission_map[expected_permission]))
          << "Permission " << expected_permission << " not set";
    }

    Permissions expected_permissions_bitset;
    for (const string& expected_permission : permissions) {
      if (expected_permission == "ALL") {
        // Set all the bits to 1.
        expected_permissions_bitset.set();
        break;
      }
      expected_permissions_bitset.set(permission_map[expected_permission]);
    }

    ASSERT_EQ(expected_permissions_bitset.to_ullong(), cached_permissions_bitset.to_ullong());
  }

};

TEST_F(TestQLPermission, TestGrantRevokeAll) {

  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);
  // Ensure permission granted to proper role.
  const string role_name = "test_role";
  const string role_name_2 = "test_role_2";
  const string role_name_3 = "test_role_3";

  CreateRole(processor, role_name);
  CreateRole(processor, role_name_2);

  const string canonical_resource_keyspaces = kRolesDataResource;
  const string grant_stmt = GrantAllKeyspaces("SELECT", role_name);
  std::vector<string> permissions_keyspaces = { "SELECT" };

  GrantRevokePermissionAndVerify(processor, grant_stmt, canonical_resource_keyspaces,
                                 permissions_keyspaces, role_name);

  // Ensure no permission granted to non-existent role.
  const string grant_stmt2 = GrantAllKeyspaces("MODIFY", role_name_3);
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt2, "Invalid Argument");

  LOG(INFO) << "Permissions version: " << GetPermissionsVersion();

  // Check multiple resources.
  const string canonical_resource_roles = kRolesRoleResource;
  const string grant_stmt3 = GrantAllRoles("DESCRIBE", role_name);
  std::vector<string> permissions_roles = { "DESCRIBE" };
  GrantRevokePermissionAndVerify(processor, grant_stmt3,
                                 canonical_resource_roles, permissions_roles, role_name);

  ExecuteValidModificationStmt(processor, grant_stmt3);

  auto select = SelectStmt(role_name);
  auto s = processor->Run(select);
  CHECK(s.ok());

  auto row_block = processor->row_block();
  EXPECT_EQ(2, row_block->row_count());  // 2 Resources found.

  auto& keyspaces_row = row_block->row(0);
  auto& roles_row = row_block->row(1);
  CheckRowContents(roles_row, canonical_resource_roles, permissions_roles, role_name);
  CheckRowContents(keyspaces_row, canonical_resource_keyspaces, permissions_keyspaces, role_name);

  // Grant another permission to test_role on all keyspaces.
  const auto grant_drop_all_keyspaces = GrantAllKeyspaces("DROP", role_name);
  permissions_keyspaces.push_back("DROP");
  GrantRevokePermissionAndVerify(processor, grant_drop_all_keyspaces, canonical_resource_keyspaces,
                                 permissions_keyspaces, role_name);

  // Revoke SELECT from test_role on all keyspaces.
  const auto revoke_select_all_keyspaces = RevokeAllKeyspaces("SELECT", role_name);
  permissions_keyspaces = { "DROP" };
  GrantRevokePermissionAndVerify(processor, revoke_select_all_keyspaces,
                                 canonical_resource_keyspaces, permissions_keyspaces, role_name);

  // Revoke DROP from test_role on all keyspaces.
  const auto revoke_drop_all_keyspaces = RevokeAllKeyspaces("DROP", role_name);
  permissions_keyspaces = {};
  GrantRevokePermissionAndVerify(processor, revoke_drop_all_keyspaces,
                                 canonical_resource_keyspaces, permissions_keyspaces, role_name);

  // Revoke DESCRIBE from test_role on all roles.
  const auto revoke_describe_all_roles = RevokeAllRoles("DESCRIBE", role_name);
  permissions_roles = {};
  GrantRevokePermissionAndVerify(processor, revoke_describe_all_roles,
                                 canonical_resource_roles, permissions_keyspaces, role_name);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = false;
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt, "Unauthorized");
}

TEST_F(TestQLPermission, TestGrantRevokeKeyspace) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);

  const string role_name = "test_role";
  const string keyspace1 = "keyspace1";
  CreateRole(processor, role_name);
  const string canonical_resource = "data/" + keyspace1;
  CreateKeyspace(processor, keyspace1);

  const string grant_stmt = GrantKeyspace("SELECT",  keyspace1, role_name);
  std::vector<string> permissions = { "SELECT" };

  // Ensure permission granted to proper role.
  GrantRevokePermissionAndVerify(processor, grant_stmt, canonical_resource, permissions,
                                 role_name);

  // Grant multiple permissions.
  const string grant_stmt2 = GrantKeyspace("MODIFY",  keyspace1, role_name);
  permissions.push_back("MODIFY");
  GrantRevokePermissionAndVerify(processor, grant_stmt2, canonical_resource, permissions,
                                 role_name);

  const string grant_stmt3 = GrantKeyspace("CREATE",  keyspace1, role_name);
  permissions.push_back("CREATE");
  GrantRevokePermissionAndVerify(processor, grant_stmt3, canonical_resource, permissions,
                                 role_name);

  // Revoke "CREATE" permission on keyspace1 from test_role.
  const auto revoke_create_stmt = RevokeKeyspace("CREATE", keyspace1, role_name);
  permissions.pop_back();
  GrantRevokePermissionAndVerify(processor, revoke_create_stmt, canonical_resource, permissions,
                                 role_name);

  // Revoke "MODIFY" permission on keyspace1 from test_role.
  const auto revoke_modify_stmt = RevokeKeyspace("MODIFY", keyspace1, role_name);
  permissions.pop_back();
  GrantRevokePermissionAndVerify(processor, revoke_modify_stmt, canonical_resource, permissions,
                                 role_name);

  // Invalid keyspace.
  const string keyspace2 = "keyspace2";
  const string grant_stmt4 = GrantKeyspace("ALTER", keyspace2, role_name);
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt4, "Resource Not Found");

  const string revoke_invalid_stmt = GrantKeyspace("ALTER", keyspace2, role_name);
  EXEC_INVALID_STMT_WITH_ERROR(revoke_invalid_stmt, "Resource Not Found");

  // Grant ALL permissions.
  const string role_name_2 = "test_role_2";
  CreateRole(processor, role_name_2);
  const string grant_stmt5 = GrantKeyspace("ALL",  keyspace1, role_name_2);
  GrantRevokePermissionAndVerify(processor, grant_stmt5, canonical_resource,
                                 all_permissions_for_keyspace, role_name_2);

  // Revoke all the permissions.
  const auto revoke_all_stmt = RevokeKeyspace("ALL", keyspace1, role_name_2);
  GrantRevokePermissionAndVerify(processor, revoke_all_stmt, canonical_resource, {}, role_name_2);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = false;
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt, "Unauthorized");
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt4, "Unauthorized");
}

TEST_F(TestQLPermission, TestGrantToRole) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);
  const string role_name = "test_role";
  const string role_name_2 = "test_role_2";

  CreateRole(processor, role_name);
  CreateRole(processor, role_name_2);

  const string canonical_resource = "roles/" + role_name_2;
  const string grant_stmt = GrantRole("AUTHORIZE", role_name_2, role_name);
  std::vector<string> permissions = { "AUTHORIZE" };

  // Ensure permission granted to proper role.
  GrantRevokePermissionAndVerify(processor, grant_stmt, canonical_resource, permissions, role_name);

  // Grant ALL permissions.
  const string grant_stmt2 = GrantRole("ALL", role_name_2, role_name);
  GrantRevokePermissionAndVerify(processor, grant_stmt2, canonical_resource,
                                 all_permissions_for_role, role_name);

  // Resource (role) not present.
  const string role_name_3 = "test_role_3";
  const string grant_stmt3 = GrantRole("DROP", role_name_3, role_name);
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt3, "Resource Not Found");

  // Lastly, create another role to verify that the roles version didn't change after all the
  // statements that didn't modify anything in the master.
  CreateRole(processor, "some_role");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = false;
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt, "Unauthorized");
}

TEST_F(TestQLPermission, TestGrantRevokeTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);

  const string role_name = "test_role";
  const string keyspace1 = "keyspace1";
  const string table1 = "table1";

  CreateRole(processor, role_name);
  CreateKeyspace(processor, keyspace1);
  CreateTable(processor, keyspace1, table1);

  const string canonical_resource = "data/" + keyspace1 + "/" + table1;
  const string table_name = keyspace1 + "." + table1;

  // Simple grant.
  const string grant_stmt = GrantTable("MODIFY", table_name, role_name);
  std::vector<string> permissions = { "MODIFY" };

  // Ensure permission granted to proper role.
  GrantRevokePermissionAndVerify(processor, grant_stmt, canonical_resource, permissions, role_name);

  // Grant with keyspace not provided.
  const auto grant_stmt2 = GrantTable("SELECT", "table1", role_name);
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt2, "Resource Not Found");

  // Grant with invalid keyspace.
  const string grant_stmt3 = GrantTable("SELECT", "keyspace2.table1", role_name);
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt3, "Resource Not Found");

  // Grant with invalid table.
  const string grant_stmt4 = GrantTable("SELECT", "keyspace1.table2", role_name);
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt4, "Resource Not Found");

  // Revoke with keyspace not provided.
  const auto invalid_revoke1 = RevokeTable("SELECT", "table1", role_name);
  EXEC_INVALID_STMT_WITH_ERROR(invalid_revoke1, "Resource Not Found");

  // Revoke with invalid keyspace.
  const auto invalid_revoke2 = RevokeTable("SELECT", "someKeyspace.table1", role_name);
  EXEC_INVALID_STMT_WITH_ERROR(invalid_revoke2, "Resource Not Found");

  // Revoke with invalid table.
  const auto invalid_revoke3 = RevokeTable("SELECT", "keyspace1.someTable", role_name);
  EXEC_INVALID_STMT_WITH_ERROR(invalid_revoke3, "Resource Not Found");

  const string revoke = RevokeTable("MODIFY", "keyspace1.table1", role_name);
  // No permissions on keyspace1.table1 should remain for role test_role.
  permissions = {};
  GrantRevokePermissionAndVerify(processor, revoke, canonical_resource, permissions, role_name);

  // Grant ALL permissions and verify that DESCRIBE is not granted.
  const string grant_stmt5 = GrantTable("ALL", table_name, role_name);
  GrantRevokePermissionAndVerify(processor, grant_stmt5, canonical_resource,
                                 all_permissions_for_table, role_name);

  // Lastly, create another role to verify that the roles version didn't change after all the
  // statements that didn't modify anything in the master.
  CreateRole(processor, "some_role");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = false;
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt, "Unauthorized");
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt3, "Unauthorized");
}

TEST_F(TestQLPermission, TestGrantDescribe) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);

  const string role1 = "test_role1";
  const string role2 = "test_role2";
  const string role3 = "test_role3";
  const string role4 = "test_role4";
  const string role5 = "test_role5";
  const string keyspace = "keyspace1";
  const string table = "table1";

  CreateRole(processor, role1);
  CreateRole(processor, role2);
  CreateRole(processor, role3);
  CreateRole(processor, role4);
  CreateRole(processor, role5);
  CreateKeyspace(processor, keyspace);
  CreateTable(processor, keyspace, table);

  const string canonical_resource = "data/" + keyspace + "/" + table;
  const string table_name = keyspace + "." + table;

  // Grant DESCRIBE on a table. It should fail.
  const string grant_stmt = GrantTable("DESCRIBE", table_name, role1);
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt, invalid_grant_describe_error_msg);

  // Grant DESCRIBE on a table that doesn't exist. It should fail with a syntax error.
  const string grant_on_invalid_table = GrantTable("DESCRIBE", "invalid_table", role1);
  EXEC_INVALID_STMT_WITH_ERROR(grant_on_invalid_table, invalid_grant_describe_error_msg);

  // Grant DESCRIBE on a table to a role that doesn't exist. It should fail with a syntax error.
  const string grant_on_table_to_invalid_role = GrantTable("DESCRIBE", table_name, "some_role");
  EXEC_INVALID_STMT_WITH_ERROR(grant_on_table_to_invalid_role, invalid_grant_describe_error_msg);

  // Grant DESCRIBE on a keyspace. It shold fail.
  const string grant_stmt2 = GrantKeyspace("DESCRIBE", keyspace, role1);
  EXEC_INVALID_STMT_WITH_ERROR(grant_stmt2, invalid_grant_describe_error_msg);

  // Grant DESCRIBE on a keyspace that doesn't exist. It should fail with a syntax error.
  const string grant_on_invalid_keyspace = GrantKeyspace("DESCRIBE", "some_keyspace", role1);
  EXEC_INVALID_STMT_WITH_ERROR(grant_on_invalid_keyspace, invalid_grant_describe_error_msg);

  // Grant DESCRIBE on a keyspace to a role that doesn't exist. It should fail with a syntax error.
  const string grant_on_keyspace_to_invalid_role = GrantKeyspace("DESCRIBE", keyspace, "some_role");
  EXEC_INVALID_STMT_WITH_ERROR(grant_on_keyspace_to_invalid_role, invalid_grant_describe_error_msg);

  // Grant DESCRIBE on all keyspaces. It should fail.
  const string grant_on_all_keyspaces = GrantAllKeyspaces("DESCRIBE", role1);
  EXEC_INVALID_STMT_WITH_ERROR(grant_on_all_keyspaces, invalid_grant_describe_error_msg);

  // Grant DESCRIBE on a role. It should fail.
  const string grant_on_role = GrantRole("DESCRIBE", role2, role1);
  EXEC_INVALID_STMT_WITH_ERROR(grant_on_role, invalid_grant_describe_error_msg);

  // Grant DESCRIBE on all roles. It should succeed.
  const string grant_on_all_roles = GrantAllRoles("DESCRIBE", role3);
  std::vector<string> permissions = {"DESCRIBE"};
  GrantRevokePermissionAndVerify(processor, grant_on_all_roles, kRolesRoleResource, permissions,
                                 role3);

  // Grant ALL on a role. It should succeed and all the appropriate permissions should be granted.
  const string grant_all_on_a_role = GrantRole("ALL", role4, role1);
  GrantRevokePermissionAndVerify(processor, grant_all_on_a_role,
                                 strings::Substitute("$0/$1", kRolesRoleResource, role4),
                                 all_permissions_for_role, role1);

  // Grant ALL on all roles. It should succeed and all the appropriate permissions should be
  // granted.
  const string grant_all_on_all_roles = GrantAllRoles("ALL", role5);
  GrantRevokePermissionAndVerify(processor, grant_all_on_all_roles, kRolesRoleResource,
                                 all_permissions_for_all_roles, role5);
}

class TestQLRole : public QLTestAuthentication {
 public:
  TestQLRole() : QLTestAuthentication() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = true;
    DCHECK(!FLAGS_ycql_allow_non_authenticated_password_reset);
  }

  // Check the granted roles for a role
  void CheckGrantedRoles(TestQLProcessor* processor, const string& role_name,
                  const std::unordered_set<string>& roles) {
    auto select = SelectStmt(role_name);
    Status s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(1, row_block->row_count());

    auto& row = row_block->row(0);

    EXPECT_EQ(InternalType::kListValue, row.column(3).type());
    QLSeqValuePB list_value = row.column(3).list_value();
    EXPECT_EQ(roles.size(), list_value.elems_size());
    for (int i = 0; i < list_value.elems_size(); i++) {
      EXPECT_TRUE(roles.find(list_value.elems(i).string_value()) != roles.end());
    }
  }

  void CheckCreateAndDropRole(TestQLProcessor* processor, const string& role_name,
                              const char* password, const bool is_superuser,
                              const bool can_login) {

    const string is_superuser_str = (is_superuser) ? "true" : "false";
    const string can_login_str = (can_login) ? "true" : "false";
    const string password_str = (password == nullptr) ? "" : Substitute(
        "AND PASSWORD =  '$0'", password);

    // Create the role
    const string create_stmt = Substitute(
        "CREATE ROLE $0 WITH LOGIN = $1 $3 AND SUPERUSER = $2;", role_name,
        can_login_str, is_superuser_str, password_str);

    ExecuteValidModificationStmt(processor, create_stmt);

    auto select = "SELECT * FROM system_auth.roles;";
    auto s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(2, row_block->row_count());
    auto& row = row_block->row(0);

    EXPECT_EQ(role_name, row.column(0).string_value());
    EXPECT_EQ(can_login, row.column(1).bool_value());
    EXPECT_EQ(is_superuser, row.column(2).bool_value());

    if (password  == nullptr) {
      EXPECT_TRUE(row.column(4).IsNull());
    } else {
      char hash[kBcryptHashSize];
      bcrypt_hashpw(password, hash);
      const auto &saved_hash = row.column(4).string_value();
      bool password_match = true;
      if (bcrypt_checkpw(password, saved_hash.c_str())) {
        password_match = false;
      }
      EXPECT_EQ(true, password_match);
    }
    const string drop_stmt = Substitute("DROP ROLE $0;", role_name);
    ExecuteValidModificationStmt(processor, drop_stmt);

    // Check that only default cassandra role exists
    auto select_after_drop = "SELECT * FROM system_auth.roles;";
    s = processor->Run(select_after_drop);

    CHECK(s.ok());
    auto row_block_after_drop = processor->row_block();
    EXPECT_EQ(1, row_block_after_drop->row_count());
    row = row_block_after_drop->row(0);
    EXPECT_EQ("cassandra", row.column(0).string_value());
  }

  void CheckRole(TestQLProcessor* processor, const string& role_name, const char* password,
                 const bool can_login, const bool is_superuser) {
    auto select = Substitute("SELECT * FROM system_auth.roles WHERE role = '$0';", role_name);

    CHECK_OK(processor->Run(select));
    auto row_block = processor->row_block();
    EXPECT_EQ(1, row_block->row_count());
    auto& row = row_block->row(0);

    EXPECT_EQ(role_name, row.column(0).string_value());
    EXPECT_EQ(can_login, row.column(1).bool_value());
    EXPECT_EQ(is_superuser, row.column(2).bool_value());

    if (password == nullptr) {
      EXPECT_TRUE(row.column(4).IsNull());
    } else {
      char hash[kBcryptHashSize];
      bcrypt_hashpw(password, hash);
      const auto& saved_hash = row.column(4).string_value();
      const bool password_match = (0 == bcrypt_checkpw(password, saved_hash.c_str()));
      EXPECT_TRUE(password_match);
    }
  }
};

TEST_F(TestQLRole, TestGrantRole) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);
  const string role_1 = "role1";
  const string role_2 = "role2";
  const string role_3 = "role3";
  const string role_4 = "role4";
  const string role_5 = "role5";
  const string role_6 = "role6";
  const string role_7 = "role7";

  CreateRole(processor, role_1);
  CreateRole(processor, role_2);
  CreateRole(processor, role_3);

  CreateRole(processor, role_5);
  CreateRole(processor, role_6);
  CreateRole(processor, role_7);

  // Single Role Granted
  GrantRole(processor, role_1, role_2);
  std::unordered_set<string> roles ( {role_1} );
  CheckGrantedRoles(processor, role_2, roles);

  // Multiple Roles Granted
  GrantRole(processor, role_3, role_2);
  roles.insert(role_3);
  CheckGrantedRoles(processor, role_2, roles);

  GrantRole(processor, role_5, role_3);
  GrantRole(processor, role_6, role_3);
  GrantRole(processor, role_7, role_5);

  // Same Grant Twice
  const string invalid_grant_1 = GrantStmt(role_1, role_2);
  EXEC_INVALID_STMT_WITH_ERROR(invalid_grant_1, Substitute("$0 is a member of $1", role_2, role_1));

  // Roles not present
  const string invalid_grant_2 = GrantStmt(role_1, role_4);
  EXEC_INVALID_STMT_WITH_ERROR(invalid_grant_2, Substitute("$0 doesn't exist", role_4));

  const string invalid_grant_3 = GrantStmt(role_4, role_2);
  EXEC_INVALID_STMT_WITH_ERROR(invalid_grant_3, Substitute("$0 doesn't exist", role_4));

  const auto invalid_circular_reference_grant = GrantStmt(role_1, role_1);
  EXEC_INVALID_STMT_WITH_ERROR(invalid_circular_reference_grant,
                               Substitute("$0 is a member of $1", role_1, role_1));

  // It should fail because role_3 was granted to role_2.
  const auto invalid_circular_reference_grant2 = GrantStmt(role_2, role_3);
  // The message is backwards, but that's what Apache Cassandra outputs.
  EXEC_INVALID_STMT_WITH_ERROR(invalid_circular_reference_grant2,
                               Substitute("$0 is a member of $1", role_3, role_2));

  CreateRole(processor, role_4);
  // Single Role Granted
  GrantRole(processor, role_4, role_3);
  roles = {role_4, role_5, role_6};
  CheckGrantedRoles(processor, role_3, roles);

  // It should fail because role_3 was granted to role_2, and role_4 granted to role3.
  const auto invalid_circular_reference_grant3 = GrantStmt(role_4, role_2);
  // The message is backwards, but that's what Apache Cassandra outputs.
  EXEC_INVALID_STMT_WITH_ERROR(invalid_circular_reference_grant3,
                               Substitute("$0 is a member of $1", role_2, role_4));

  // It should fail because role_4 was granted to role_3, and role_3 to role_2.
  const auto invalid_circular_reference_grant4 = GrantStmt(role_2, role_4);
  // The message is backwards, but that's what Apache Cassandra outputs.
  EXEC_INVALID_STMT_WITH_ERROR(invalid_circular_reference_grant4,
                               Substitute("$0 is a member of $1", role_4, role_2));

  // It should fail because role_7 -> role_5 -> role_3 -> role_2.
  const auto invalid_circular_reference_grant5 = GrantStmt(role_2, role_7);
  // The message is backwards, but that's what Apache Cassandra outputs.
  EXEC_INVALID_STMT_WITH_ERROR(invalid_circular_reference_grant5,
                               Substitute("$0 is a member of $1", role_7, role_2));

  // Lastly, create another role to verify that the roles version didn't change after all the
  // statements that didn't modify anything in the master.
  CreateRole(processor, "some_role");
}

TEST_F(TestQLRole, TestRevokeRole) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);
  const string role1 = "role1";
  const string role2 = "role2";
  const string role3 = "role3";
  const string role4 = "role4";
  CreateRole(processor, role1);
  CreateRole(processor, role2);
  CreateRole(processor, role3);

  // Grant roles role1 and role3 to role2.
  GrantRole(processor, role1, role2);
  GrantRole(processor, role3, role2);

  std::unordered_set<string> roles ( {role1, role3} );
  CheckGrantedRoles(processor, role2, roles);

  // Revoke role1 from role2.
  RevokeRole(processor, role1, role2);

  // Verify that the only granted role to role2 is role3.
  roles = {role3};
  CheckGrantedRoles(processor, role2, roles);

  // Revoke the last granted role and verify it.
  RevokeRole(processor, role3, role2);
  roles = {};
  CheckGrantedRoles(processor, role2, roles);

  // Revoke role from itself. It should fail silently.
  RevokeRoleIgnored(processor, role1, role1);

  // Lastly, create another role to verify that the roles version didn't change after all the
  // statements that didn't modify anything in the master.
  CreateRole(processor, "some_role");

  // Try to revoke it again. It should fail.
  const auto invalid_revoke = RevokeStmt(role3, role2);
  EXEC_INVALID_STMT_WITH_ERROR(
      invalid_revoke, Substitute("$0 is not a member of $1", role2, role3));
}

TEST_F(TestQLRole, TestRoleQuerySimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);
  CheckCreateAndDropRole(processor, "test_role1", "test_pw", true, true);
  // Test no password set
  CheckCreateAndDropRole(processor, "test_role2", nullptr, false, true);
}

TEST_F(TestQLRole, TestQLCreateRoleSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS (CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);

  // Valid Create Role Statements
  const string role1 = "manager1;";
  const string role2 = "\"manager2\";";
  const string role3 = "'manager3';";
  const string role4 = "manager4 WITH PASSWORD = '!3@Ab' AND LOGIN = false AND SUPERUSER = true;";
  const string role5 = "manager5 WITH SUPERUSER = false AND PASSWORD =  '!3@Ab' AND LOGIN = true;";
  const string role6 = "manager6 WITH LOGIN = true;";
  const string role7 = "manager7 WITH PASSWORD = '!3@Ab';";
  const string role8 = "manager8 WITH SUPERUSER = true";
  const string role12 = "'mAnaGer3';";

  // Invalid Create Role Statements : Duplicate Attributes
  const string role9 = "manager9 WITH PASSWORD = '!3@Ab' AND PASSWORD = '!3@ss';";
  const string role10 = "manager10 WITH LOGIN = true AND PASSWORD = '!3@ss' and LOGIN = true;";
  const string role11 = "manager11 WITH SUPERUSER = false AND SUPERUSER = true;";
  const string role13 = "manager12 WITH LOGIN = true AND LOGIN = true;";

  // Options unsupported
  const string role14 = "manager13 WITH OPTIONS = { 'opt_1' : 'opt_val_1'};";
  const string role15 = "manager14 WITH SUPERUSER = false AND OPTIONS = { 'opt_1' : 'opt_val_1'};";

  // Create role1, role_name is simple identifier
  ExecuteValidModificationStmt(processor, CreateStmt(role1));

  // Create role2, role_name is quoted identifier
  ExecuteValidModificationStmt(processor, CreateStmt(role2));

  // Create role3, role_name is string
  ExecuteValidModificationStmt(processor, CreateStmt(role3));

  // Create role4, all attributes are present
  ExecuteValidModificationStmt(processor, CreateStmt(role4));

  // Create role5, permute the attributes
  ExecuteValidModificationStmt(processor, CreateStmt(role5));

  // Create role6, only attribute LOGIN
  ExecuteValidModificationStmt(processor, CreateStmt(role6));

  // Create role7, only attribute PASSWORD
  ExecuteValidModificationStmt(processor, CreateStmt(role7));

  // Create role8, only attribute SUPERUSER
  ExecuteValidModificationStmt(processor, CreateStmt(role8));

  // Create role12, role_name preserves capitalization
  ExecuteValidModificationStmt(processor, CreateStmt(role12));

  // Verify that all 'CREATE TABLE' statements fail for tables that have already been created.

  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role1));
  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role2));
  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role3));
  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role4));
  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role5));
  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role6));
  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role7));
  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role8));
  EXEC_DUPLICATE_CREATE_ROLE_STMT(CreateStmt(role12));

  // Verify that all 'CREATE TABLE IF EXISTS' statements succeed for tables that have already been
  // created.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role1));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role2));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role3));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role4));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role5));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role6));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role7));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role8));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(role12));

  // Invalid Statements
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(role9), "Invalid Role Definition");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(role10), "Invalid Role Definition");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(role11), "Invalid Role Definition");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(role13), "Invalid Role Definition");

  // Single role_option
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(role14), "Feature Not Supported");

  // Multiple role_options
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(role15), "Feature Not Supported");

  // Lastly, create another role to verify that the roles version didn't change after all the
  // statements that didn't modify anything in the master.
  CreateRole(processor, "another_role");

  // Flag Test:
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = false;
  for (bool non_authenticated_password_reset : {false, true}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_allow_non_authenticated_password_reset) =
        non_authenticated_password_reset;
    EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(role4), "Unauthorized");  // Valid, but unauthorized
    EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(role9), "Unauthorized");  // Invalid and unauthorized
  }
}

TEST_F(TestQLRole, TestQLDropRoleSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS (CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);

  // Valid Create Role Statements
  const string role1 = "manager1;";
  const string role2 = "\"manager2\";";
  const string role3 = "'manager3';";

  ExecuteValidModificationStmt(processor, CreateStmt(role1));        // Create role
  ExecuteValidModificationStmt(processor, CreateStmt(role2));        // Create role
  ExecuteValidModificationStmt(processor, CreateStmt(role3));        // Create role

  // Check all variants of role_name
  ExecuteValidModificationStmt(processor, DropStmt(role1));          // Drop role
  ExecuteValidModificationStmt(processor, DropStmt(role2));          // Drop role
  ExecuteValidModificationStmt(processor, DropStmt(role3));          // Drop role

  // Check if subsequent drop generates errors
  EXEC_INVALID_STMT_WITH_ERROR(DropStmt(role1), "Role Not Found");
  EXEC_INVALID_STMT_WITH_ERROR(DropStmt(role2), "Role Not Found");
  EXEC_INVALID_STMT_WITH_ERROR(DropStmt(role3), "Role Not Found");

  // These statements will not change the roles' version in the master.
  EXEC_VALID_STMT(DropIfExistsStmt(role1));   // Check if exists
  EXEC_VALID_STMT(DropIfExistsStmt(role2));   // Check if exists
  EXEC_VALID_STMT(DropIfExistsStmt(role3));   // Check if exists

  // Lastly, create another role to verify that the roles version didn't change after all the
  // statements that didn't modify anything in the master.
  CreateRole(processor, "some_role");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = false;
  for (bool non_authenticated_password_reset : {false, true}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_allow_non_authenticated_password_reset) =
        non_authenticated_password_reset;
    EXEC_INVALID_STMT_WITH_ERROR(DropStmt(role1), "Unauthorized");
  }
}

TEST_F(TestQLRole, TestQLAlterRoleSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);

  // Valid Create Role Statements.
  const string role1 = "normal_user";
  const string role2 = "super_user";

  CreateRole(processor, role1);
  CreateRole(processor, role2, /*superuser=*/ true);

  // Check all variants of role_name.
  ExecuteValidModificationStmt(processor, AlterStmt(role1, "UPDATED_PWD"));
  CheckRole(processor, role1, "UPDATED_PWD", /*can_login*/ true, /*is_superuser*/ false);
  ExecuteValidModificationStmt(processor, AlterStmt(role2, "UPDATED_PWD"));
  CheckRole(processor, role2, "UPDATED_PWD", /*can_login*/ true, /*is_superuser*/ true);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = false;
  for (bool non_authenticated_password_reset : {false, true}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_allow_non_authenticated_password_reset) =
        non_authenticated_password_reset;
    if (non_authenticated_password_reset) {
      ExecuteValidModificationStmt(processor, AlterStmt(role1, "UPDATED_WITH_DISABLED_AUTH"));
      ExecuteValidModificationStmt(processor, AlterStmt(role2, "UPDATED_WITH_DISABLED_AUTH"));
    } else {
      EXEC_INVALID_STMT_WITH_ERROR(AlterStmt(role1, "UPDATED_WITH_DISABLED_AUTH"), "Unauthorized");
      EXEC_INVALID_STMT_WITH_ERROR(AlterStmt(role2, "UPDATED_WITH_DISABLED_AUTH"), "Unauthorized");
    }
  }
}

// Test that whenever we remove a role, this role is removed from the member_of field of all the
// other roles.
TEST_F(TestQLRole, TestQLDroppedRoleIsRemovedFromMemberOfField) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS (CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);

  // Valid Create Role Statements
  const string role1 = "r1";
  const string role2 = "r2";
  const string role3 = "r3";

  CreateRole(processor, role1);
  CreateRole(processor, role2);
  CreateRole(processor, role3);

  GrantRole(processor, role1, role2);
  std::unordered_set<string> roles ( {role1} );
  CheckGrantedRoles(processor, role2, roles);

  GrantRole(processor, role1, role3);
  CheckGrantedRoles(processor, role3, roles);

  ExecuteValidModificationStmt(processor, DropStmt(role1));          // Drop role

  roles = {};
  CheckGrantedRoles(processor, role2, roles);

  roles = {};
  CheckGrantedRoles(processor, role3, roles);
}

// When a mutation doesn't get committed or aborted during a GRANT or REVOKE request, subsequent
// requests will time out.
TEST_F(TestQLRole, TestMutationsGetCommittedOrAborted) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor(kDefaultCassandraUsername);
  // Ensure permission granted to proper role
  const string role1 = "test_role_1";
  const string role2 = "test_role_2";

  CreateRole(processor, role1);
  CreateRole(processor, role2);

  GrantRole(processor, role1, role2);

  // Same Grant Twice
  const auto invalid_grant = GrantStmt(role1, role2);
  EXEC_INVALID_STMT_WITH_ERROR(invalid_grant, Substitute("$0 is a member of $1", role2, role1));

  // Verify that the same invalid operation fails instead of timing out.
  EXEC_INVALID_STMT_WITH_ERROR(invalid_grant, Substitute("$0 is a member of $1", role2, role1));

  RevokeRole(processor, role1, role2);

  // Revoke it again.
  const auto invalid_revoke = RevokeStmt(role1, role2);
  EXEC_INVALID_STMT_WITH_ERROR(
      invalid_revoke, Substitute("$0 is not a member of $1", role2, role1));

  // If the mutation was ended properly, this request shouldn't time out.
  EXEC_INVALID_STMT_WITH_ERROR(
      invalid_revoke, Substitute("$0 is not a member of $1", role2, role1));

  // Lastly, create another role to verify that the roles version didn't change after all the
  // statements that didn't modify anything in the master.
  CreateRole(processor, "another_role");
}

} // namespace ql
} // namespace yb
