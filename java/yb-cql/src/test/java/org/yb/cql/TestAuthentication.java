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
package org.yb.cql;

import com.datastax.driver.core.*;
import com.datastax.driver.core.ProtocolOptions.Compression;

import com.datastax.driver.core.exceptions.UnauthorizedException;
import org.junit.Test;

import static org.yb.AssertionWrappers.*;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestAuthentication extends BaseAuthenticationCQLTest {
  @Test(timeout = 100000)
  public void testCreateRoles() throws Exception {
    testCreateRoleHelper("role1", "$!@$q1<>?", false, false);
    testCreateRoleHelper("role4", "$!@$q1<>?", false, true);
    testCreateRoleHelper("role2", "$!@$q1<>?", true, false);
    testCreateRoleHelper("role3", "$!@$q1<>?", true, true);
  }

  @Test(timeout = 100000)
  public void testDeleteExistingRole() throws Exception {
    // Create the role.
    String roleName = "role_test";
    String password = "adsfQ%T!qaewfa";
    testCreateRoleHelper(roleName, password, true, false);

    Session s = getDefaultSession();
    // Delete the role.
    String deleteStmt = String.format("DROP ROLE %s", roleName);
    s.execute(deleteStmt);

    // Verify that we cannot connect using the deleted role.
    checkConnectivity(true, roleName, password, true);
  }

  @Test(timeout = 100000)
  public void testDeleteNonExistingRole() throws Exception {
    Session s = getDefaultSession();

    // Delete a role that doesn't exist.
    String deleteStmt = "DROP ROLE other_role";
    thrown.expect(com.datastax.driver.core.exceptions.InvalidQueryException.class);
    thrown.expectMessage("Role other_role does not exist");
    s.execute(deleteStmt);
  }

  @Test(timeout = 100000)
  public void testAlterPasswordForExistingRole() throws Exception {
    Session s = getDefaultSession();

    String roleName = "alter_test_1";
    String oldPassword = "!%^()(*~`";
    testCreateRoleHelper(roleName, oldPassword, true, false);

    // Change the role's password.
    String newPassword = "%#^$%@$@";
    String alterStmt = String.format("ALTER ROLE %s with PASSWORD = '%s'", roleName, newPassword);
    s.execute(alterStmt);

    verifyRole(roleName, true, false);

    // Verify that we cannot connect using the old password.
    checkConnectivity(true, roleName, oldPassword, true);

    // Verify that we can connect using the new password.
    checkConnectivity(true, roleName, newPassword, false);
  }

  @Test(timeout = 100000)
  public void testAlterLoginForExistingRole() throws Exception {
    Session s = getDefaultSession();

    String roleName = "alter_test_2";
    String password = "!%^()(*~`";
    testCreateRoleHelper(roleName, password, true, false);

    // Verify that we can login.
    checkConnectivity(true, roleName, password, false);

    // Change the role LOGIN from true to false.
    String alterStmt = String.format("ALTER ROLE %s with LOGIN = false", roleName);
    s.execute(alterStmt);

    verifyRole(roleName, false, false);

    // Verify that we cannot longer login.
    checkConnectivity(true, roleName, password, true);
  }

  @Test(timeout = 100000)
  public void testAlterSuperuserForExistingRole() throws Exception {
    Session s = getDefaultSession();

    String roleName = "alter_test_3";
    String password = "!%^()(*~`";
    testCreateRoleHelper(roleName, password, true, false);

    // Verify that we can login.
    checkConnectivity(true, roleName, password, false);

    // Make the role a super user.
    String alterStmt = String.format("ALTER ROLE %s with SUPERUSER = true", roleName);
    s.execute(alterStmt);

    verifyRole(roleName, true, true);

    // Verify that we can still login. Making a role a super user shouldn't affect connectivity.
    checkConnectivity(true, roleName, password, false);
  }

  @Test(timeout = 100000)
  public void testAlterPasswordAndLoginAndSuperuserExistingRole() throws Exception {
    Session s = getDefaultSession();

    String roleName = "alter_test_4";
    String oldPassword = "sdf$hgfaY13";
    testCreateRoleHelper(roleName, oldPassword, false, false);

    // Verify that we cannot login because the role was created with LOGIN = false.
    checkConnectivity(true, roleName, oldPassword, true);

    // Change the role's password, LOGIN from false to true, and SUPERUSER from false to true.
    String newPassword = "*&oi2jr8OI";
    String alterStmt = String.format(
        "ALTER ROLE %s with PASSWORD = '%s' AND LOGIN = true AND SUPERUSER = true",
        roleName, newPassword);
    s.execute(alterStmt);

    verifyRole(roleName, true, true);

    // Verify that we can login.
    checkConnectivity(true, roleName, newPassword, false);
  }

  @Test(timeout = 100000)
  public void testAlterNonExistingRole() throws Exception {
    Session s = getDefaultSession();
    String alterStmt = "ALTER ROLE alter_test_5 WITH LOGIN = false";

    thrown.expect(com.datastax.driver.core.exceptions.InvalidQueryException.class);
    thrown.expectMessage("Role alter_test_5 does not exist");
    s.execute(alterStmt);
  }

  @Test(timeout = 100000)
  public void testConnectWithDefaultUserPass() throws Exception {
    checkConnectivity(true, "cassandra", "cassandra", false);
  }

  @Test(timeout = 100000)
  public void testConnectWithFakeUserPass() throws Exception {
    checkConnectivity(true, "fakeUser", "fakePass", true);
  }

  @Test(timeout = 100000)
  public void testConnectNoUserPass() throws Exception {
    checkConnectivity(false, null, null, true);
  }

  @Test(timeout = 100000)
  public void testConnectWithDefaultUserPassAndCompression() throws Exception {
    checkConnectivity(true, "cassandra", "cassandra", Compression.LZ4, false);
    checkConnectivity(true, "cassandra", "cassandra", Compression.SNAPPY, false);
    checkConnectivity(true, "fakeUser", "fakePass", Compression.LZ4, true);
    checkConnectivity(true, "fakeUser", "fakePass", Compression.SNAPPY, true);
    checkConnectivity(false, null, null, Compression.LZ4, true);
    checkConnectivity(false, null, null, Compression.SNAPPY, true);
  }

  @Test
  public void testNonSuperuserRoleCannotCreateSuperuserRole() throws Exception {
    Session s = getDefaultSession();
    String roleName = "non_superuser";
    String password = "abc";
    testCreateRoleHelperWithSession(roleName, password, true, false, false, s);

    grantPermission(CREATE, ALL_ROLES, "", roleName, s);

    Session s2 = getSession(roleName, password);
    // Verify that we can create a simple role.
    s2.execute("CREATE ROLE simple_role");

    // Verify that we can't create a superuser role.
    thrown.expect(UnauthorizedException.class);
    s2.execute("CREATE ROLE some_superuser_role WITH SUPERUSER = true");
  }

  @Test
  public void testNonSuperuserRoleCannotCreateIfNotExistsSuperuserRole() throws Exception {
    Session s = getDefaultSession();
    String roleName = "non_superuser_2";
    String password = "abc";
    testCreateRoleHelperWithSession(roleName, password, true, false, false, s);

    grantPermission(CREATE, ALL_ROLES, "", roleName, s);

    Session s2 = getSession(roleName, password);
    // Verify that we can create a simple role.
    s2.execute("CREATE ROLE simple_role_2");

    // Verify that CREATE IF NOT EXISTS for an existing role, fails with an unauthorized exception.
    thrown.expect(UnauthorizedException.class);
    s2.execute("CREATE ROLE IF NOT EXISTS simple_role_2 WITH SUPERUSER = true");
  }

  @Test
  public void testSuperuserRoleCanCreateSuperuserRole() throws Exception {
    Session s = getDefaultSession();
    String roleName = "superuser_role";
    String password = "abc";
    testCreateRoleHelperWithSession(roleName, password, true, true, false, s);

    // No need to grant permissions to the superuser role.
    Session s2 = getSession(roleName, password);
    s2.execute("CREATE ROLE some_superuser_role WITH SUPERUSER = true");
  }
}
