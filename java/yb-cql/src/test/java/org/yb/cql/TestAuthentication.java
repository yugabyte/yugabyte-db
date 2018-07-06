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

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;

import org.junit.BeforeClass;
import org.junit.Test;

import static org.junit.Assert.*;

public class TestAuthentication extends BaseCQLTest {
  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    // Setting verbose level for debugging.
    BaseCQLTest.tserverArgs = Arrays.asList("--use_cassandra_authentication=true");
    BaseCQLTest.setUpBeforeClass();
  }

  public Cluster.Builder getDefaultClusterBuilder() {
    // Default return cassandra/cassandra auth.
    return super.getDefaultClusterBuilder().withCredentials("cassandra", "cassandra");
  }

  public Session getDefaultSession() {
    Cluster.Builder cb = getDefaultClusterBuilder();
    Cluster c = cb.build();
    Session s = c.connect();
    return s;
  }

  // Verifies that roleName exists in the system_auth.roles table, and that canLogin and isSuperuser
  // match the fields 'can_login' and 'is_superuser' for this role.
  private void verifyRole(String roleName, boolean canLogin, boolean isSuperuser)
      throws Exception {
    verifyRoleFields(roleName, canLogin, isSuperuser, new ArrayList<>());
  }

  private void verifyRoleFields(String roleName, boolean canLogin, boolean isSuperuser,
                                List<String> memberOf) throws Exception {
    Session s = getDefaultSession();
    ResultSet rs = s.execute(
        String.format("SELECT * FROM system_auth.roles WHERE role = '%s';", roleName));

    Iterator<Row> iter = rs.iterator();
    assertTrue(String.format("Unable to find role '%s'", roleName), iter.hasNext());
    Row r = iter.next();
    assertEquals(r.getBool("can_login"), canLogin);
    assertEquals(r.getBool("is_superuser"), isSuperuser);
    assertEquals(r.getList("member_of", String.class), memberOf);
  }

  private void testCreateRoleHelper(String roleName, String password, boolean canLogin,
                                    boolean isSuperuser) throws Exception {
    Session s = getDefaultSession();

    // Create the role.
    String createStmt = String.format(
        "CREATE ROLE %s WITH PASSWORD = '%s' AND LOGIN = %s AND SUPERUSER = %s",
        roleName, password, canLogin, isSuperuser);

    s.execute(createStmt);

    // Verify that we can connect using the new role.
    checkConnectivity(true, roleName, password, !canLogin);

    // Verify that the information got written into system_auth.roles correctly.
    verifyRole(roleName, canLogin, isSuperuser);
  }

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
    String alterStmt = "ALTER ROLE alter_test_5";

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

  public void checkConnectivity(
      boolean usingAuth, String optUser, String optPass, boolean expectFailure) {
    checkConnectivity(usingAuth, optUser, optPass, Compression.NONE, expectFailure);
  }

  public void checkConnectivity(boolean usingAuth,
                                String optUser,
                                String optPass,
                                Compression compression,
                                boolean expectFailure) {
    // Use superclass definition to not have a default set of credentials.
    Cluster.Builder cb = super.getDefaultClusterBuilder();
    Cluster c = null;
    if (usingAuth) {
      cb = cb.withCredentials(optUser, optPass);
    }
    if (compression != Compression.NONE) {
      cb = cb.withCompression(compression);
    }
    c = cb.build();
    try {
      Session s = c.connect();
      s.execute("SELECT * FROM system_auth.roles;");
      // If we're expecting a failure, we should NOT be in here.
      assertFalse(expectFailure);
    } catch (com.datastax.driver.core.exceptions.AuthenticationException e) {
      // If we're expecting a failure, we should be in here.
      assertTrue(expectFailure);
    }
  }
}
