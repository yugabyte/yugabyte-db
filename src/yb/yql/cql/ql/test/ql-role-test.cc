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
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/gutil/strings/substitute.h"

DECLARE_bool(use_cassandra_authentication);

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
  do {                                                                  \
    Status s = processor->Run(stmt);                                    \
    EXPECT_FALSE(s.ok());                                               \
    EXPECT_FALSE(s.ToString().find("Duplicate Role. Already present") == string::npos); \
  } while (false)

#define EXEC_INVALID_STMT_MSG(stmt, msg)                                \
  do {                                                                  \
    Status s = processor->Run(stmt);                                    \
    ASSERT_FALSE(s.ok());                                               \
    ASSERT_FALSE(s.ToString().find(msg) == string::npos);               \
  } while (false)

class TestQLPermission : public QLTestBase {
 public:
  TestQLPermission() : QLTestBase() {
    FLAGS_use_cassandra_authentication = true;
  }

  // Helper Functions
  void CreateTable(TestQLProcessor* processor, const string& keyspace_name,
                   const string& table_name) {
    const string create_stmt = Substitute(
        "CREATE TABLE $0.$1(id int, name varchar, primary key(id));", keyspace_name, table_name);
    Status s = processor->Run(create_stmt);
    CHECK(s.ok());
  }

  void CreateKeyspace(TestQLProcessor* processor, const string& keyspace_name) {
    const string create_stmt = Substitute(
        "CREATE KEYSPACE $0;", keyspace_name);
    Status s = processor->Run(create_stmt);
    CHECK(s.ok());
  }

  void CreateRole(TestQLProcessor* processor, const string& role_name) {
    const string create_stmt = Substitute(
        "CREATE ROLE $0 WITH LOGIN = TRUE AND SUPERUSER = TRUE AND PASSWORD = 'TEST';", role_name);
    Status s = processor->Run(create_stmt);
    CHECK(s.ok());
  }

  string GrantAllKeyspaces(const string& permission, const string& role_name) const {
    return Substitute("GRANT $0 ON ALL KEYSPACES TO $1;", permission, role_name);
  }

  string GrantKeyspace(const string& permission, const string& keyspace,
                            const string& role_name) const {
    return Substitute("GRANT $0 ON KEYSPACE $1 TO $2;", permission, keyspace, role_name);
  }

  string GrantTable(const string& permission, const string& table, const string& role_name) const {
    return Substitute("GRANT $0 ON TABLE $1 TO $2;", permission, table, role_name);
  }

  string GrantAllRoles(const string& permission, const string& role_name) const {
    return Substitute("GRANT $0 ON ALL ROLES TO $1;", permission, role_name);
  }

  string GrantRole(const string& permission, const string& role_resource,
                        const string& role_name) const {
    return Substitute("GRANT $0 ON ROLE $1 TO $2;", permission, role_resource, role_name);
  }

  string SelectStmt(const string& role_name) const {
    return Substitute("SELECT * FROM system_auth.role_permissions where role='$0';", role_name);
  }

  void CheckRowContents(const QLRow& row, const string& canonical_resource,
                        const std::vector<string> &permissions, const string& role_name) {
    EXPECT_EQ(role_name, row.column(0).string_value());
    EXPECT_EQ(canonical_resource, row.column(1).string_value());

    EXPECT_EQ(QLValue::InternalType::kListValue, row.column(2).type());
    QLSeqValuePB list_value = row.column(2).list_value();
    EXPECT_EQ(permissions.size(), list_value.elems_size());
    // Create a set of the values:
    std::unordered_set<string> permissions_set;
    for (int i = 0; i < permissions.size(); i++) {
      permissions_set.insert(list_value.elems(i).string_value());
    }

    for (const auto& permission : permissions) {
      EXPECT_TRUE(permissions_set.find(permission) != permissions_set.end());
    }
  }

  // Generic Select
  void CheckPermission(TestQLProcessor* processor, const string& grant_stmt,
                       const string& canonical_resource,
                       const std::vector<string> &permissions, const string& role_name){

    LOG (INFO) << "Running Statement" << grant_stmt;
    Status s = processor->Run(grant_stmt);
    CHECK(s.ok());

    auto select = SelectStmt(role_name);
    s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(1, row_block->row_count());

    QLRow &row = row_block->row(0);
    CheckRowContents(row, canonical_resource, permissions, role_name);
  }

};

TEST_F(TestQLPermission, TestGrantAll) {

  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  // Ensure permission granted to proper role
  const string role_name = "test_role";
  const string role_name_2 = "test_role_2";
  const string role_name_3 = "test_role_3";

  CreateRole(processor, role_name);
  CreateRole(processor, role_name_2);


  const string canonical_resource_keyspaces = "data";
  const string grant_stmt = GrantAllKeyspaces("SELECT", role_name);
  std::vector<string> permissions_keyspaces = { "SELECT" };

  CheckPermission(processor, grant_stmt, canonical_resource_keyspaces, permissions_keyspaces,
                  role_name);

  // Ensure no permission granted to non-existent role
  const string grant_stmt2 = GrantAllKeyspaces("MODIFY", role_name_3);
  EXEC_INVALID_STMT_MSG(grant_stmt2, "Invalid Argument");

  // Check Multiple resources
  const string canonical_resource_roles = "roles";
  const string grant_stmt3 = GrantAllRoles("DESCRIBE", role_name);
  std::vector<string> permissions_roles = { "DESCRIBE" };

  Status s = processor->Run(grant_stmt3);
  CHECK(s.ok());

  auto select = SelectStmt(role_name);
  s = processor->Run(select);
  CHECK(s.ok());

  auto row_block = processor->row_block();
  EXPECT_EQ(2, row_block->row_count());  // 2 Resources found

  QLRow& keyspaces_row = row_block->row(0);
  QLRow& roles_row = row_block->row(1);
  CheckRowContents(roles_row, canonical_resource_roles, permissions_roles, role_name);
  CheckRowContents(keyspaces_row, canonical_resource_keyspaces, permissions_keyspaces, role_name);

  FLAGS_use_cassandra_authentication = false;
  EXEC_INVALID_STMT_MSG(grant_stmt, "Unauthorized");
}

TEST_F(TestQLPermission, TestGrantKeyspace) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  // Ensure permission granted to proper role
  const string role_name = "test_role";
  const string keyspace1 = "keyspace1";
  CreateRole(processor, role_name);
  const string canonical_resource = "data/" + keyspace1;
  CreateKeyspace(processor, keyspace1);

  const string grant_stmt = GrantKeyspace("SELECT",  keyspace1, role_name);
  std::vector<string> permissions = { "SELECT" };
  CheckPermission(processor, grant_stmt, canonical_resource, permissions, role_name);

  // Grant Multiple Permissions
  const string grant_stmt2 = GrantKeyspace("MODIFY",  keyspace1, role_name);
  permissions.push_back("MODIFY");
  CheckPermission(processor, grant_stmt2, canonical_resource, permissions, role_name);

  const string grant_stmt3 = GrantKeyspace("CREATE",  keyspace1, role_name);
  permissions.push_back("CREATE");
  CheckPermission(processor, grant_stmt3, canonical_resource, permissions, role_name);

  // Keyspace not present
  const string keyspace2 = "keyspace2";
  const string grant_stmt4 = GrantKeyspace("ALTER", keyspace2, role_name);
  EXEC_INVALID_STMT_MSG(grant_stmt4, "Resource Not Found");

  // Grant All permissions
  const string role_name_2 = "test_role_2";
  CreateRole(processor, role_name_2);
  const string grant_stmt5 = GrantKeyspace("ALL",  keyspace1, role_name_2);
  std::vector<string> permissions_2 =
      { "ALTER", "AUTHORIZE", "CREATE", "DESCRIBE", "DROP", "MODIFY", "SELECT" };
  CheckPermission(processor, grant_stmt5, canonical_resource, permissions_2, role_name_2);

  FLAGS_use_cassandra_authentication = false;
  EXEC_INVALID_STMT_MSG(grant_stmt, "Unauthorized");
  EXEC_INVALID_STMT_MSG(grant_stmt4, "Unauthorized");
}

TEST_F(TestQLPermission, TestGrantRole) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  // Ensure permission granted to proper role
  const string role_name = "test_role";
  const string role_name_2 = "test_role_2";

  CreateRole(processor, role_name);
  CreateRole(processor, role_name_2);

  const string canonical_resource = "roles/" + role_name_2;
  const string grant_stmt = GrantRole("AUTHORIZE", role_name_2, role_name);
  std::vector<string> permissions = { "AUTHORIZE" };
  CheckPermission(processor, grant_stmt, canonical_resource, permissions, role_name);

  // Resource (role) not present
  const string role_name_3 = "test_role_3";
  const string grant_stmt2 = GrantRole("DROP", role_name_3, role_name);
  EXEC_INVALID_STMT_MSG(grant_stmt2, "Resource Not Found");

  FLAGS_use_cassandra_authentication = false;
  EXEC_INVALID_STMT_MSG(grant_stmt, "Unauthorized");
}

TEST_F(TestQLPermission, TestGrantTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  // Ensure permission granted to proper role
  const string role_name = "test_role";
  const string keyspace1 = "keyspace1";
  const string table1 = "table1";

  CreateRole(processor, role_name);
  CreateKeyspace(processor, keyspace1);
  CreateTable(processor, keyspace1, table1);

  const string canonical_resource = "data/keyspace1/table1";
  const string grant_stmt = GrantTable("MODIFY", "keyspace1.table1", role_name);
  std::vector<string> permissions = { "MODIFY" };
  CheckPermission(processor, grant_stmt, canonical_resource, permissions, role_name);

  // keyspace absent
  const string grant_stmt2 = GrantTable("SELECT", "keyspace2.table1", role_name);
  EXEC_INVALID_STMT_MSG(grant_stmt2, "Resource Not Found");

  // Table absent
  const string grant_stmt3 = GrantTable("SELECT", "keyspace1.table2", role_name);
  EXEC_INVALID_STMT_MSG(grant_stmt3, "Resource Not Found");

  FLAGS_use_cassandra_authentication = false;
  EXEC_INVALID_STMT_MSG(grant_stmt, "Unauthorized");
  EXEC_INVALID_STMT_MSG(grant_stmt2, "Unauthorized");
}

class TestQLRole : public QLTestBase {
 public:
  TestQLRole() : QLTestBase() {
    FLAGS_use_cassandra_authentication = true;
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

  static const string GrantStmt(const string& role, const string& recipient) {
    return Substitute("GRANT $0 to $1", role, recipient);
  }

  void CreateRole(TestQLProcessor* processor, const string& role_name) {
    const string create_stmt = Substitute(
        "CREATE ROLE $0 WITH LOGIN = TRUE AND SUPERUSER = TRUE AND PASSWORD = 'TEST';", role_name);
    Status s = processor->Run(create_stmt);
    CHECK(s.ok());
  }

  void GrantRole(TestQLProcessor* processor, const string& role, const string& recipient) {
    const string grant_stmt = GrantStmt(role, recipient);
    Status s = processor->Run(grant_stmt);
    CHECK(s.ok());
  }

  string SelectStmt(const string& role_name) const {
    return Substitute("SELECT * FROM system_auth.roles where role='$0';", role_name);
  }

  // Check the granted roles for a role
  void CheckGrantedRoles(TestQLProcessor* processor, const string& role_name,
                  const std::unordered_set<string>& roles) {
    auto select = SelectStmt(role_name);
    Status s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(1, row_block->row_count());

    QLRow &row = row_block->row(0);

    EXPECT_EQ(QLValue::InternalType::kListValue, row.column(3).type());
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

    Status s = processor->Run(create_stmt);
    CHECK(s.ok());

    auto select = "SELECT * FROM system_auth.roles;";
    s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(2, row_block->row_count());
    QLRow &row = row_block->row(0);

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
    s = processor->Run(drop_stmt);
    CHECK(s.ok());

    // Check that only default cassandra role exists
    auto select_after_drop = "SELECT * FROM system_auth.roles;";
    s = processor->Run(select_after_drop);

    CHECK(s.ok());
    auto row_block_after_drop = processor->row_block();
    EXPECT_EQ(1, row_block_after_drop->row_count());
    row = row_block_after_drop->row(0);
    EXPECT_EQ("cassandra", row.column(0).string_value());
  }
};

TEST_F(TestQLRole, TestGrantRole) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  const string role_1 = "role1";
  const string role_2 = "role2";
  const string role_3 = "role3";
  const string role_4 = "role4";
  CreateRole(processor, role_1);
  CreateRole(processor, role_2);
  CreateRole(processor, role_3);

  // Single Role Granted
  GrantRole(processor, role_1, role_2);
  std::unordered_set<string> roles ( {role_1} );
  CheckGrantedRoles(processor, role_2, roles);

  // Multiple Roles Granted
  GrantRole(processor, role_3, role_2);
  roles.insert(role_3);
  CheckGrantedRoles(processor, role_2, roles);

  // Same Grant Twice
  const string invalid_grant_1 = GrantStmt(role_1, role_2);
  EXEC_INVALID_STMT_MSG(invalid_grant_1, "Invalid Request");

  // Roles not present
  const string invalid_grant_2 = GrantStmt(role_1, role_4);
  EXEC_INVALID_STMT_MSG(invalid_grant_2, "Role Not Found");

  const string invalid_grant_3 = GrantStmt(role_4, role_2);
  EXEC_INVALID_STMT_MSG(invalid_grant_3, "Role Not Found");
  // TODO (Bristy) : Add in test to check circular grant of roles.

}

TEST_F(TestQLRole, TestRoleQuerySimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  CheckCreateAndDropRole(processor, "test_role1", "test_pw", true, true);
  // Test no password set
  CheckCreateAndDropRole(processor, "test_role2", nullptr, false, true);
}

TEST_F(TestQLRole, TestQLCreateRoleSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS (CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor* processor = GetQLProcessor();

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
  EXEC_VALID_STMT(CreateStmt(role1));

  // Create role2, role_name is quoted identifier
  EXEC_VALID_STMT(CreateStmt(role2));

  // Create role3, role_name is string
  EXEC_VALID_STMT(CreateStmt(role3));

  // Create role4, all attributes are present
  EXEC_VALID_STMT(CreateStmt(role4));

  // Create role5, permute the attributes
  EXEC_VALID_STMT(CreateStmt(role5));

  // Create role6, only attribute LOGIN
  EXEC_VALID_STMT(CreateStmt(role6));

  // Create role7, only attribute PASSWORD
  EXEC_VALID_STMT(CreateStmt(role7));

  // Create role8, only attribute SUPERUSER
  EXEC_VALID_STMT(CreateStmt(role8));

  // Create role14, role_name preserves capitalization
  EXEC_VALID_STMT(CreateStmt(role12));


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

  EXEC_INVALID_STMT_MSG(CreateStmt(role9), "Invalid Role Definition");
  EXEC_INVALID_STMT_MSG(CreateStmt(role10), "Invalid Role Definition");
  EXEC_INVALID_STMT_MSG(CreateStmt(role11), "Invalid Role Definition");
  EXEC_INVALID_STMT_MSG(CreateStmt(role13), "Invalid Role Definition");

  // Single role_option
  EXEC_INVALID_STMT_MSG(CreateStmt(role14), "Feature Not Supported");
  // Multiple role_options
  EXEC_INVALID_STMT_MSG(CreateStmt(role15), "Feature Not Supported");

  // Flag Test:
  FLAGS_use_cassandra_authentication = false;;
  EXEC_INVALID_STMT_MSG(CreateStmt(role4), "Unauthorized");  // Valid, but unauthorized
  EXEC_INVALID_STMT_MSG(CreateStmt(role9), "Unauthorized");  // Invalid and unauthorized
}

TEST_F(TestQLRole, TestQLDropRoleSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS (CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor* processor = GetQLProcessor();

  // Valid Create Role Statements
  const string role1 = "manager1;";
  const string role2 = "\"manager2\";";
  const string role3 = "'manager3';";


  EXEC_VALID_STMT(CreateStmt(role1));        // Create role
  EXEC_VALID_STMT(CreateStmt(role2));        // Create role
  EXEC_VALID_STMT(CreateStmt(role3));        // Create role

  // Check all variants of role_name
  EXEC_VALID_STMT(DropStmt(role1));          // Drop role
  EXEC_VALID_STMT(DropStmt(role2));          // Drop role
  EXEC_VALID_STMT(DropStmt(role3));          // Drop role

  // Check if subsequent drop generates errors
  EXEC_INVALID_STMT_MSG(DropStmt(role1), "Role Not Found");
  EXEC_INVALID_STMT_MSG(DropStmt(role2), "Role Not Found");
  EXEC_INVALID_STMT_MSG(DropStmt(role3), "Role Not Found");


  EXEC_VALID_STMT(DropIfExistsStmt(role1));   // Check if exists
  EXEC_VALID_STMT(DropIfExistsStmt(role2));   // Check if exists
  EXEC_VALID_STMT(DropIfExistsStmt(role3));   // Check if exists

  FLAGS_use_cassandra_authentication = false;
  EXEC_INVALID_STMT_MSG(DropStmt(role1), "Unauthorized");
}

} // namespace ql
} // namespace yb
