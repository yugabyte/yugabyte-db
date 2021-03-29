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

import static org.yb.AssertionWrappers.*;

import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;

import com.datastax.driver.core.ProtocolOptions;
import com.datastax.driver.core.ProtocolOptions.Compression;
import com.datastax.driver.core.exceptions.UnauthorizedException;

@RunWith(value = YBTestRunner.class)
public class TestAuthentication extends BaseAuthenticationCQLTest {
  /**
   * Wait duration after role change so that permission cache refreshes with latest permission
   * derived from value of update_permissions_cache_msecs gflag, adding 100 ms for good measure.
   */
  protected int getSleepInterval() {
    int permissionUpdateInterval = 2000;
    return permissionUpdateInterval + 100;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("update_permissions_cache_msecs", String.valueOf(getSleepInterval()));
    return flagMap;
  }


  @Test(timeout = 100000)
  public void testDeleteExistingRole() throws Exception {
    // Create the role.
    String roleName = "role_test";
    String password = "adsfQ%T!qaewfa";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), roleName, password, true, false, true);
    }

    // Delete the role.
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      cs.execute(String.format("DROP ROLE %s", roleName));
    }
    Thread.sleep(getSleepInterval());

    // Verify that we cannot connect using the deleted role.
    checkConnectivity(true, roleName, password, true);
  }

  @Test
  public void testSuperuserCanDeleteAnotherSuperuserRole() throws Exception {
    String superuser1 = "superuser_1";
    String superuser2 = "superuser_2";
    String pwd = "password";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), superuser1, pwd, true, true, true);
    }

    try (ClusterAndSession cs2 = connectWithCredentials(superuser1, pwd)) {
      // Create a new role using superuser1's session. This should grant all the permissions on role
      // superuser2 to superuser1.
      createRole(cs2.getSession(), superuser2, pwd, true, true, true);

      // Verify that superuser1 can delete superuser2.
      cs2.execute(String.format("DROP ROLE %s", superuser2));
    }
    Thread.sleep(getSleepInterval());

    // Verify that we can't connect using the deleted role.
    checkConnectivity(true, superuser2, pwd, true);
  }

  @Test(timeout = 100000)
  public void testAlterPasswordForExistingRole() throws Exception {
    String roleName = "alter_test_1";
    String oldPassword = "!%^()(*~`";
    String newPassword = "%#^$%@$@";
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), roleName, oldPassword, true, false, true);

      // Change the role's password.
      cs.execute(String.format("ALTER ROLE %s with PASSWORD = '%s'", roleName, newPassword));
    }
    Thread.sleep(getSleepInterval());

    // During the first iteration we check tha the changes were applied correctly to the
    // in-memory structures. Then we restart the cluster to verify that the changes were saved
    // to disk and are loaded correctly.
    for (int i = 0; i < 2; i++) {
      try (ClusterAndSession cs = connectWithTestDefaults()) {
        verifyRole(cs.getSession(), roleName, true, false);
      }

      // Verify that we cannot connect using the old password.
      checkConnectivity(true, roleName, oldPassword, true);

      // Verify that we can connect using the new password.
      checkConnectivity(true, roleName, newPassword, false);

      if (i == 0) {
        restartYcqlMiniCluster();
      }
    }
  }

  @Test(timeout = 100000)
  public void testAlterLoginForExistingRole() throws Exception {
    String roleName = "alter_test_2";
    String password = "!%^()(*~`";
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), roleName, password, true, false, true);

      // Verify that we can login.
      checkConnectivity(true, roleName, password, false);

      // Change the role LOGIN from true to false.
      cs.execute(String.format("ALTER ROLE %s with LOGIN = false", roleName));
    }
    Thread.sleep(getSleepInterval());

    // During the first iteration we check tha the changes were applied correctly to the
    // in-memory structures. Then we restart the cluster to verify that the changes were saved
    // to disk and are loaded correctly.
    for (int i = 0; i < 2; i++) {
      try (ClusterAndSession cs = connectWithTestDefaults()) {
        verifyRole(cs.getSession(), roleName, false, false);
      }

      // Verify that we cannot longer login.
      checkConnectivity(true, roleName, password, true);

      if (i == 0) {
        restartYcqlMiniCluster();
      }
    }
  }

  @Test(timeout = 100000)
  public void testAlterPasswordAndLoginAndSuperuserExistingRole() throws Exception {
    String roleName = "alter_test_4";
    String oldPassword = "sdf$hgfaY13";
    String newPassword = "*&oi2jr8OI";
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), roleName, oldPassword, false, false, true);

      // Verify that we cannot login because the role was created with LOGIN = false.
      checkConnectivity(true, roleName, oldPassword, true);

      // Change the role's password, LOGIN from false to true, and SUPERUSER from false to true.
      cs.execute(String.format(
          "ALTER ROLE %s with PASSWORD = '%s' AND LOGIN = true AND SUPERUSER = true",
          roleName, newPassword));
    }
    Thread.sleep(getSleepInterval());

    // During the first iteration we check tha the changes were applied correctly to the
    // in-memory structures. Then we restart the cluster to verify that the changes were saved
    // to disk and are loaded correctly.
    for (int i = 0; i < 2; i++) {
      try (ClusterAndSession cs = connectWithTestDefaults()) {
        verifyRole(cs.getSession(), roleName, true, true);
      }

      // Verify that we can login.
      checkConnectivity(true, roleName, newPassword, false);

      if (i == 0) {
        restartYcqlMiniCluster();
      }
    }
  }

  @Test(timeout = 100000)
  public void testCreateRoles() throws Exception {
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), "role1", "$!@$q1<>?", false, false, true);
      createRole(cs.getSession(), "role4", "$!@$q1<>?", false, true, true);
      createRole(cs.getSession(), "role2", "$!@$q1<>?", true, false, true);
      createRole(cs.getSession(), "role3", "$!@$q1<>?", true, true, true);
    }
  }

  @Test(timeout = 100000)
  public void testDeleteNonExistingRole() throws Exception {
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      // Delete a role that doesn't exist.
      String deleteStmt = "DROP ROLE other_role";
      thrown.expect(com.datastax.driver.core.exceptions.InvalidQueryException.class);
      thrown.expectMessage("Role other_role does not exist");
      cs.execute(deleteStmt);
    }
  }

  @Test
  public void testNoSuperuserCannotDeleteSuperuserRole() throws Exception {
    String superuser = "superuser1";
    String user = "no_superuser";
    String pwd = "password";

    // For now we create this user as a superuser. We will change that attribute later.
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), user, pwd, true, true, true);

      try (ClusterAndSession cs2 = connectWithCredentials(user, pwd)) {
        // Create another superuser role using user's session so that it gets all the permissions on
        // the new role.
        createRole(cs2.getSession(), superuser, pwd, true, true, false);

        cs.execute(String.format("ALTER ROLE %s WITH SUPERUSER = false", user));

        // A non-superuser role tries to delete a superuser role.
        thrown.expect(com.datastax.driver.core.exceptions.UnauthorizedException.class);
        thrown.expectMessage("Only superusers can drop a role with superuser status");
        cs2.execute(String.format("DROP ROLE %s", superuser));
      }
    }
  }

  @Test(timeout = 100000)
  public void testAlterSuperuserForExistingRole() throws Exception {
    String roleName = "alter_test_3";
    String password = "!%^()(*~`";
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), roleName, password, true, false, true);

      // Verify that we can login.
      checkConnectivity(true, roleName, password, false);

      // Make the role a super user.
      String alterStmt = String.format("ALTER ROLE %s with SUPERUSER = true", roleName);
      cs.execute(alterStmt);
    }

    // During the first iteration we check tha the changes were applied correctly to the
    // in-memory structures. Then we restart the cluster to verify that the changes were saved
    // to disk and are loaded correctly.
    for (int i = 0; i < 2; i++) {
      try (ClusterAndSession cs = connectWithTestDefaults()) {
        verifyRole(cs.getSession(), roleName, true, true);
      }

      // Verify that we can still login. Making a role a super user shouldn't affect connectivity.
      checkConnectivity(true, roleName, password, false);

      if (i == 0) {
        restartYcqlMiniCluster();
      }
    }
  }

  @Test(timeout = 100000)
  public void testAlterNonExistingRole() throws Exception {
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      String alterStmt = "ALTER ROLE alter_test_5 WITH LOGIN = false";

      thrown.expect(com.datastax.driver.core.exceptions.InvalidQueryException.class);
      thrown.expectMessage("Role alter_test_5 does not exist");
      cs.execute(alterStmt);
    }
  }

  @Test(timeout = 100000)
  public void testConnectWithDefaultUserPass() throws Exception {
    checkConnectivity(true, "cassandra", "cassandra", false);
  }

  @Test(timeout = 100000)
  public void testConnectWithWrongPass() throws Exception {
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), "someuser", "somepass", true, true, true);
    }
    checkConnectivity(true, "someuser", "wrongpass", true);
  }

  @Test(timeout = 100000)
  public void testConnectWithNoLogin() throws Exception {
    String createStmt = String.format("CREATE ROLE 'usernnologin' WITH PASSWORD='abc'");
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      cs.execute(createStmt);
    }
    checkConnectivityWithMessage(true, "usernnologin", "abc",
        ProtocolOptions.Compression.NONE,true, "is not permitted to log in");
  }

  @Test(timeout = 100000)
  public void testConnectWihtNoLoginAndNoPass() throws Exception {
    String createStmt = String.format("CREATE ROLE 'usernnologinnopass'");
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      cs.execute(createStmt);
    }
    checkConnectivityWithMessage(true, "usernnologinnopass", "abc",
        ProtocolOptions.Compression.NONE,true, "and/or password are incorrect");
  }

  // This tests fix for https://github.com/yugabyte/yugabyte-db/issues/4459.
  // Before this fix, the tserver process would crash when trying to check the credentials because
  // it couldn't handle roles that were created without a password.
  @Test(timeout = 100000)
    public void testConnectWithRoleThatHasNoPass() throws Exception {
    String createStmt = String.format("CREATE ROLE 'usernopass' WITH LOGIN = true");
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      cs.execute(createStmt);
    }
    checkConnectivity(true, "usernopass", "abc", true);
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
    String roleName = "non_superuser";
    String password = "abc";
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), roleName, password, true, false, false);
      grantPermission(cs.getSession(), CREATE, ALL_ROLES, "", roleName);
    }

    try (ClusterAndSession cs2 = connectWithCredentials(roleName, password)) {
      // Verify that we can create a simple role.
      cs2.execute("CREATE ROLE simple_role");

      // Verify that we can't create a superuser role.
      thrown.expect(UnauthorizedException.class);
      cs2.execute("CREATE ROLE some_superuser_role WITH SUPERUSER = true");
    }
  }

  @Test
  public void testNonSuperuserRoleCannotCreateIfNotExistsSuperuserRole() throws Exception {
    String roleName = "non_superuser_2";
    String password = "abc";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), roleName, password, true, false, false);
      grantPermission(cs.getSession(), CREATE, ALL_ROLES, "", roleName);
    }

    try (ClusterAndSession cs2 = connectWithCredentials(roleName, password)) {
      // Verify that we can create a simple role.
      cs2.execute("CREATE ROLE simple_role_2");

      // Verify that CREATE IF NOT EXISTS for an existing role,
      // fails with an unauthorized exception.
      thrown.expect(UnauthorizedException.class);
      cs2.execute("CREATE ROLE IF NOT EXISTS simple_role_2 WITH SUPERUSER = true");
    }
  }

  @Test
  public void testSuperuserRoleCanCreateSuperuserRole() throws Exception {
    String roleName = "superuser_role";
    String password = "abc";
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), roleName, password, true, true, false);
    }

    // No need to grant permissions to the superuser role.
    try (ClusterAndSession cs2 = connectWithCredentials(roleName, password)) {
      cs2.execute("CREATE ROLE some_superuser_role WITH SUPERUSER = true");
    }
  }

  @Test
  public void testAlterSuperuserFieldOfSuperuserRole() throws Exception {
    String password = "abc";
    String superuser = "alter_superuser_test_superuser";
    String nonSuperuser = "alter_superuser_test_non_superuser";

    // Initially create this user with the superuser status, so that it can create another
    // another superuser. We will remove this role' superuser status before trying to alter a
    // superuser role.
    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), nonSuperuser, password, true, true, true);
    }

    try (ClusterAndSession cs = connectWithTestDefaults();
         ClusterAndSession cs2 = connectWithCredentials(nonSuperuser, password)) {
      // Using nonSuperuser role to create superuser so that it automatically acquires all the
      // permissions on superuser.
      createRole(cs2.getSession(), superuser, password, true, true, true);

      // Remove the superuser status from nonSuperuser to trigger the failure.
      cs.execute(String.format("ALTER ROLE %s WITH SUPERUSER = FALSE",
                               nonSuperuser));

      // A non-superuser role tries to alter the superuser field of a superuser role.
      String alterStmt = String.format("ALTER ROLE %s WITH SUPERUSER = FALSE", superuser);
      thrown.expect(com.datastax.driver.core.exceptions.UnauthorizedException.class);
      thrown.expectMessage("Only superusers are allowed to alter superuser status");
      cs2.execute(alterStmt);
    }
  }

  // The previous test verifies that a non-superuser role cannot alter the superuser status of a
  // superuser role. This test verifies that a non-superuser role cannot modify the superuser status
  // of a non-superuser role.
  @Test
  public void testAlterSuperuserFieldOfNonSuperuserRole() throws Exception {
    String password = "abc";
    String nonSuperuser = "nosuperuser_role_123";
    String anotherNonSuperuser = "another_nosuperuser_role_123";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), nonSuperuser, password, true /* canLogin */,
          false /* isSuperuser */, true /* verifyConnectivity */);
      cs.execute(String.format("GRANT ALL ON ALL ROLES to %s", nonSuperuser));
      cs.execute(String.format("GRANT ALL ON ALL KEYSPACES to %s", nonSuperuser));
    }

    try (ClusterAndSession cs2 = connectWithCredentials(nonSuperuser, password)) {
      // Using nonSuperuser role to create anotherNonSuperuser so that it automatically acquires all
      // the permissions on anotherNonSuperuser.
      createRole(cs2.getSession(), anotherNonSuperuser, password, true /* canLogin */,
          false /* isSuperuser */, false /* verifyConnectivity */);

      // Try to modify the superuser status from anotherNonSuperuser to trigger the failure.
      String alterStmt = String.format("ALTER ROLE %s WITH SUPERUSER = FALSE", anotherNonSuperuser);
      thrown.expect(com.datastax.driver.core.exceptions.UnauthorizedException.class);
      thrown.expectMessage("Only superusers are allowed to alter superuser status");
      cs2.execute(alterStmt);
    }
  }

  @Test
  public void testRoleCannotDeleteItself() throws Exception {
    String user = "delete_itself_role";
    String password = "abc";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), user, password, true, true, true);
    }

    try (ClusterAndSession cs2 = connectWithCredentials(user, password)) {
      thrown.expect(com.datastax.driver.core.exceptions.InvalidQueryException.class);
      thrown.expectMessage("Cannot DROP primary role for current login");
      cs2.execute(String.format("DROP ROLE %s", user));
    }
  }

  @Test
  public void testDuplicatePasswordIsValid() throws Exception {
    String role1 = "duplicate_password_role1";
    String role2 = "duplicate_password_role2";
    String password = "abc";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      createRole(cs.getSession(), role1, password, true, false, true);
      createRole(cs.getSession(), role2, password, true, false, true);
    }

    try (ClusterAndSession cs1 = connectWithCredentials(role1, password);
         ClusterAndSession cs2 = connectWithCredentials(role2, password)) {
      // Nothing to do here.
    }
  }

  @Test
  public void testMultipleLoginWithinCache() throws Exception {
    int roleCount = 4;
    String password = "abc";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      for (int i = 0; i < roleCount; i++) {
        String role = String.format("cache_role%d", i);
        createRole(cs.getSession(), role, password, true, false, true);
      }
    }

    for (int j = 0; j < 3; j++) {
      for (int i = 0; i < roleCount; i++) {
        String role = String.format("cache_role%d", i);
        try (ClusterAndSession cs1 = connectWithCredentials(role, password)) {
          // Nothing to do here.
        }
      }
    }
  }

  @Test(timeout=500000)
  public void testLoginExhaustCache() throws Exception {
    int roleCount = 10;
    String password = "abc";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      for (int i = 0; i < roleCount; i++) {
        String role = String.format("exhaust_role%d", i);
        createRole(cs.getSession(), role, password, true, false, true);
      }
    }

    for (int i = 0; i < roleCount; i++) {
      String role = String.format("exhaust_role%d", i);
      try (ClusterAndSession cs1 = connectWithCredentials(role, password)) {
        // Nothing to do here.
      }
    }
  }
}
