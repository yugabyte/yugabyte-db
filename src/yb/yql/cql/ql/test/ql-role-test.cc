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

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/gutil/strings/substitute.h"


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

#define EXEC_DUPLICATE_CREATE_ROLE_STMT(ql_stmt)                                                 \
do {                                                                                             \
  Status s = processor->Run(ql_stmt);                                                            \
  EXPECT_FALSE(s.ok());                                                                          \
  EXPECT_FALSE(s.ToString().find("Duplicate Role. Already present") == string::npos);            \
} while (false)

#define EXEC_INVALID_CREATE_ROLE_STMT(ql_stmt, msg)                                              \
do {                                                                                             \
  Status s = processor->Run(ql_stmt);                                                            \
  ASSERT_FALSE(s.ok());                                                                          \
  ASSERT_FALSE(s.ToString().find(msg) == string::npos);                                          \
} while (false)

#define EXEC_INVALID_DROP_ROLE_STMT(ql_stmt, msg)                                                \
do {                                                                                             \
  Status s = processor->Run(ql_stmt);                                                            \
  ASSERT_FALSE(s.ok());                                                                          \
  ASSERT_FALSE(s.ToString().find(msg) == string::npos);                                          \
} while (false)


class TestQLRole : public QLTestBase {
 public:
  TestQLRole() : QLTestBase() {
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

  void CheckCreateAndDropRole(TestQLProcessor *processor, const string& role_name,
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

    Status s = processor->Run(create_stmt.c_str());
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
    s = processor->Run(drop_stmt.c_str());
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

TEST_F(TestQLRole, TestRoleQuerySimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  CheckCreateAndDropRole(processor, "test_role1", "test_pw", true, true);
  // Test no password set
  CheckCreateAndDropRole(processor, "test_role2", nullptr, false, true);
}

TEST_F(TestQLRole, TestQLCreateRoleSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS (CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

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

  EXEC_INVALID_CREATE_ROLE_STMT(CreateStmt(role9), "Invalid Role Definition");
  EXEC_INVALID_CREATE_ROLE_STMT(CreateStmt(role10), "Invalid Role Definition");
  EXEC_INVALID_CREATE_ROLE_STMT(CreateStmt(role11), "Invalid Role Definition");
  EXEC_INVALID_CREATE_ROLE_STMT(CreateStmt(role13), "Invalid Role Definition");

  // Single role_option
  EXEC_INVALID_CREATE_ROLE_STMT(CreateStmt(role14), "Feature Not Supported");
  // Multiple role_options
  EXEC_INVALID_CREATE_ROLE_STMT(CreateStmt(role15), "Feature Not Supported");
}

TEST_F(TestQLRole, TestQLDropRoleSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS (CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

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
  EXEC_INVALID_DROP_ROLE_STMT(DropStmt(role1), "Role Not Found");
  EXEC_INVALID_DROP_ROLE_STMT(DropStmt(role2), "Role Not Found");
  EXEC_INVALID_DROP_ROLE_STMT(DropStmt(role3), "Role Not Found");


  EXEC_VALID_STMT(DropIfExistsStmt(role1));   // Check if exists
  EXEC_VALID_STMT(DropIfExistsStmt(role2));   // Check if exists
  EXEC_VALID_STMT(DropIfExistsStmt(role3));   // Check if exists
}

} // namespace ql
} // namespace yb
