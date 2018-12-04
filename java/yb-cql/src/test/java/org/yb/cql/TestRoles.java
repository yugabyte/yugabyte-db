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

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.exceptions.InvalidQueryException;
import com.datastax.driver.core.exceptions.SyntaxError;

import com.datastax.driver.core.exceptions.UnauthorizedException;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static junit.framework.TestCase.fail;
import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value=YBTestRunner.class)
public class TestRoles extends BaseAuthenticationCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestRoles.class);
  private String[] role = new String[10];

  // Permissions in the same order as in catalog_manager.cc.
  private static final List<String> ALL_PERMISSIONS =
      Arrays.asList("ALTER", "AUTHORIZE", "CREATE", "DESCRIBE", "DROP", "MODIFY", "SELECT");

  private static final List<String> ALL_PERMISSIONS_EXCEPT_DESCRIBE =
      Arrays.asList("ALTER", "AUTHORIZE", "CREATE", "DROP", "MODIFY", "SELECT");

  private static final String DESCRIBE_SYNTAX_ERROR_MSG =
      "Resource type DataResource does not support any of the requested permissions";

  private Session s;

  @Before
  public void setupSession() {
    s = getDefaultSession();
  }

  private void CreateRoles(int n) throws Exception {
    StackTraceElement[] stackTraceElements = Thread.currentThread().getStackTrace();
    String callersName = stackTraceElements[2].getMethodName().toLowerCase();
    for (int i = 0; i < n; i++) {
      role[i] = String.format("role_%d_%s", i, callersName);
      LOG.info("role name: " + role[i]);
      CreateSimpleRole(role[i]);
    }
  }

  private void assertRoleGranted(String role, List<String> memberOf) {
    List<Row> rows = s.execute(
        String.format("SELECT * FROM system_auth.roles WHERE role = '%s';", role)).all();
    assertEquals(1, rows.size());

    List list = rows.get(0).getList("member_of", String.class);
    assertEquals(memberOf.size(), list.size());

    for (String expectedRole : memberOf) {
      if (!list.contains(expectedRole)) {
        fail("Unable to find member " + expectedRole);
      }
    }
  }

  private String GrantStmt(String memberOfRole, String role) {
    return String.format("GRANT %s to %s", memberOfRole, role);
  }

  private String RevokeStmt(String memberOfRole, String role) {
    return String.format("REVOKE %s FROM %s", memberOfRole, role);
  }

  private String GrantPermissionKeyspaceStmt(String permission, String keyspace, String role) {
    return String.format("GRANT %s ON KEYSPACE %s TO %s", permission, keyspace, role);
  }

  private String GrantPermissionAllKeyspacesStmt(String permission, String role) {
    return String.format("GRANT %s ON ALL KEYSPACES TO %s", permission, role);
  }

  private String GrantPermissionTableStmt(String permission, String table, String role) {
    return String.format("GRANT %s ON TABLE %s TO %s", permission, table, role);
  }

  private String GrantPermissionRoleStmt(String permission, String role, String roleRecipient) {
    return String.format("GRANT %s ON ROLE %s TO %s", permission, role, roleRecipient);
  }

  private String GrantPermissionAllRolesStmt(String permission, String role) {
    return String.format("GRANT %s ON ALL ROLES TO %s", permission, role);
  }

  private String RevokePermissionKeyspaceStmt(String permission, String keyspace, String role) {
    return String.format("REVOKE %s ON KEYSPACE %s FROM %s", permission, keyspace, role);
  }

  private String RevokePermissionAllKeyspacesStmt(String permission, String role) {
    return String.format("REVOKE %s ON ALL KEYSPACES FROM %s", permission, role);
  }

  private String RevokePermissionTableStmt(String permission, String table, String role) {
    return String.format("REVOKE %s ON TABLE %s FROM %s", permission, table, role);
  }

  private String RevokePermissionRoleStmt(String permission, String role, String roleRecipient) {
    return String.format("REVOKE %s ON ROLE %s FROM %s", permission, role, roleRecipient);
  }

  private String RevokePermissionAllRolesStmt(String permission, String role) {
    return String.format("REVOKE %s ON ALL ROLES FROM %s", permission, role);
  }

  private void CreateSimpleRole(String roleName) throws Exception {
    String password = ";oqr94t";
    testCreateRoleHelper(roleName, password, true, false);
  }

  @Test
  public void testGrantRole() throws Exception {
    // Create the roles.
    CreateRoles(2);

    // Grant role[1] to role[0].
    s.execute(GrantStmt(role[1], role[0]));

    assertRoleGranted(role[0], Arrays.asList(role[1]));
  }

  @Test
  public void testGrantRoleCircularReference() throws Exception {
    // Create the roles.
    CreateRoles(4);

    // Grant role[1] to role[0].
    s.execute(GrantStmt(role[1], role[0]));
    assertRoleGranted(role[0], Arrays.asList(role[1]));

    // Try to grant role[0] to role[1] to create a circular reference. It should fail.
    thrown.expect(InvalidQueryException.class);
    thrown.expectMessage(String.format("%s is a member of %s", role[1], role[0]));
    s.execute(GrantStmt(role[0], role[1]));

    assertRoleGranted(role[1], Arrays.asList());

    // Grant role[2] to role[1].
    s.execute(GrantStmt(role[2], role[1]));
    assertRoleGranted(role[1], Arrays.asList(role[2]));

    // Try to grant role[2] to role[0] to create a circular reference. It should fail.
    thrown.expect(InvalidQueryException.class);
    thrown.expectMessage(String.format("%s is a member of %s", role[0], role[2]));
    s.execute(GrantStmt(role[2], role[0]));
    assertRoleGranted(role[0], Arrays.asList(role[1]));

    // Try to grant role[0] to role[2] to create a circular reference. It should fail.
    thrown.expect(InvalidQueryException.class);
    thrown.expectMessage(String.format("%s is a member of %s", role[2], role[0]));
    s.execute(GrantStmt(role[0], role[2]));
    assertRoleGranted(role[2], Arrays.asList());
  }

  @Test
  public void testGrantInvalidRole() throws Exception {
    // Create one role.
    CreateRoles(1);

    // Try to grant a role that doesn't exist to role[0].
    String invalidRole = "invalid_role";
    thrown.expect(InvalidQueryException.class);
    thrown.expectMessage(String.format("%s doesn't exist", invalidRole));
    s.execute(GrantStmt(invalidRole, role[0]));
  }

  @Test
  public void testCannotGrantRoleToItself() throws Exception {
    // Create a role.
    CreateRoles(1);

    // Try to grant role[0] to role[0]. It should fail.
    thrown.expect(InvalidQueryException.class);
    s.execute(GrantStmt(role[0], role[0]));
  }

  @Test
  public void testRevokeSeveralRoles() throws Exception {
    // Create the roles.
    CreateRoles(4);

    s.execute(GrantStmt(role[1], role[0]));
    s.execute(GrantStmt(role[2], role[0]));
    s.execute(GrantStmt(role[3], role[0]));
    assertRoleGranted(role[0], Arrays.asList(role[1], role[2], role[3]));

    s.execute(RevokeStmt(role[2], role[0]));
    assertRoleGranted(role[0], Arrays.asList(role[1], role[3]));

    s.execute(RevokeStmt(role[1], role[0]));
    assertRoleGranted(role[0], Arrays.asList(role[3]));
  }

  @Test
  public void testRevokeRoleFromItself() throws Exception {
    // Create the roles.
    CreateRoles(2);

    // Grant role[1] to role[0].
    s.execute(GrantStmt(role[1], role[0]));
    assertRoleGranted(role[0], Arrays.asList(role[1]));

    // Try to revoke role[0] from role[0]. It should return without an error although nothing
    // will happen.
    s.execute(RevokeStmt(role[0], role[0]));

    // Verify that role0's member_of field didn't get modified.
    assertRoleGranted(role[0], Arrays.asList(role[1]));
  }

  @Test
  public void testRevokeRole() throws Exception {
    // Create the roles.
   CreateRoles(2);

    s.execute(GrantStmt(role[1], role[0]));
    assertRoleGranted(role[0], Arrays.asList(role[1]));

    // Revoke role[1] from role[0].
    s.execute(RevokeStmt(role[1], role[0]));
    assertRoleGranted(role[0], Arrays.asList());
  }

  public interface GrantRevokeStmt {
    String get(String permission, String resource, String role) throws Exception;
  }

  private void testGrantRevoke(String resourceName, String canonicalResource,
      GrantRevokeStmt grantStmt, GrantRevokeStmt revokeStmt, List<String> allPermissions)
      throws Exception {

    s.execute(grantStmt.get(allPermissions.get(0), resourceName, role[0]));
    assertPermissionsGranted(s, role[0], canonicalResource, Arrays.asList(allPermissions.get(0)));

    s.execute(revokeStmt.get(allPermissions.get(0), resourceName, role[0]));
    assertPermissionsGranted(s, role[0], canonicalResource, Arrays.asList());

    // Grant all the permissions one by one.
    List<String> grantedPermissions = new ArrayList<>();
    for (String permission: allPermissions) {
      s.execute(grantStmt.get(permission, resourceName, role[0]));
      grantedPermissions.add(permission);
      assertPermissionsGranted(s, role[0], canonicalResource, grantedPermissions);
    }

    // Revoke all the permissions one by one.
    for (String permission: allPermissions) {
      LOG.info("Revoking permission " + permission);
      s.execute(revokeStmt.get(permission, resourceName, role[0]));
      grantedPermissions.remove(0);
      assertPermissionsGranted(s, role[0], canonicalResource, grantedPermissions);
    }

    // Grant all the permissions at once.
    s.execute(grantStmt.get("ALL", resourceName, role[0]));
    assertPermissionsGranted(s, role[0], canonicalResource, allPermissions);

    // Revoke all the permissions at once.
    s.execute(revokeStmt.get("ALL", resourceName, role[0]));
    assertPermissionsGranted(s, role[0], canonicalResource, Arrays.asList());

    // Grant all the permissions at once.
    s.execute(grantStmt.get("ALL", resourceName, role[0]));
    assertPermissionsGranted(s, role[0], canonicalResource, allPermissions);

    // Revoke one of the permissions in the middle.
    s.execute(revokeStmt.get("DROP", resourceName, role[0]));
    List<String> permissions = new ArrayList<>();
    permissions.addAll(allPermissions);
    permissions.remove(permissions.indexOf("DROP"));
    assertPermissionsGranted(s, role[0], canonicalResource, permissions);

    if (permissions.contains("DESCRIBE")) {
      // Revoke another permission in the middle.
      s.execute(revokeStmt.get("DESCRIBE", resourceName, role[0]));
      permissions.remove(permissions.indexOf("DESCRIBE"));
      assertPermissionsGranted(s, role[0], canonicalResource, permissions);
    }

    // Revoke the first permission.
    s.execute(revokeStmt.get("ALTER", resourceName, role[0]));
    permissions.remove(permissions.indexOf("ALTER"));
    assertPermissionsGranted(s, role[0], canonicalResource, permissions);

    // Revoke the last permission.
    s.execute(revokeStmt.get("AUTHORIZE", resourceName, role[0]));
    permissions.remove(permissions.indexOf("AUTHORIZE"));
    assertPermissionsGranted(s, role[0], canonicalResource, permissions);
  }

  @Test
  public void testGrantRevokeKeyspace() throws Exception {

    CreateRoles(1);

    String resourceName = "test_grant_revoke_keyspace";
    createKeyspace(resourceName);

    testGrantRevoke(resourceName, "data/" + resourceName,
        (String permission, String resource, String role) ->
            GrantPermissionKeyspaceStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionKeyspaceStmt(permission, resource, role),
        ALL_PERMISSIONS_EXCEPT_DESCRIBE);
  }

  @Test
  public void testGrantRevokeTable() throws Exception {
    CreateRoles(1);

    String keyspace = "test_grant_revoke_table";
    createKeyspace(keyspace);
    useKeyspace(keyspace);

    String table = "table_test";
    createTable(table);
    String resourceName = String.format("%s.%s", keyspace, table);

    testGrantRevoke(resourceName, String.format("data/%s/%s", keyspace, table),
        (String permission, String resource, String role) ->
            GrantPermissionTableStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionTableStmt(permission, resource, role),
        ALL_PERMISSIONS_FOR_TABLE);
  }

  @Test
  public void testGrantRevokeRole() throws Exception {
    CreateRoles(2);

    testGrantRevoke(role[1], String.format("roles/%s", role[1]),
        (String permission, String resource, String role) ->
            GrantPermissionRoleStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionRoleStmt(permission, resource, role),
        ALL_PERMISSIONS_FOR_ROLE);
  }

  @Test
  public void testGrantRevokeOnAllRoles() throws Exception {
    CreateRoles(1);

    // dummy_role will never be used because resource is ignored when we call
    // GrantPermissionAllRolesStmt and RevokePermissionAllRolesStmt.
    testGrantRevoke("dummy_role", "roles",
        (String permission, String resource, String role) ->
            GrantPermissionAllRolesStmt(permission, role),
        (String permission, String resource, String role) ->
            RevokePermissionAllRolesStmt(permission, role),
        ALL_PERMISSIONS_FOR_ALL_ROLES);
  }

  @Test
  public void testGrantRevokeOnAllKeyspaces() throws Exception {
    CreateRoles(1);

    // Create a few keyspaces
    for (int i = 0; i < 10; i++) {
      createKeyspace("test_grant_revoke_on_all_keyspaces_" + Integer.toString(i));
    }

    // dummy_keyspace will never be used because resource is ignored when we call
    // GrantPermissionAllKeyspacesStmt and RevokePermissionAllKeyspacesStmt.
    testGrantRevoke("dummy_keyspace", "data",
        (String permission, String resource, String role) ->
            GrantPermissionAllKeyspacesStmt(permission, role),
        (String permission, String resource, String role) ->
            RevokePermissionAllKeyspacesStmt(permission, role),
        ALL_PERMISSIONS_FOR_ALL_KEYSPACES);
  }

  // If the statement is "ON ALL KEYSPACES" or "ON ALL ROLES" resource name should be an empty
  // string so we don't run a statement with an invalid resource since it will be ignored and will
  // not throw the expected exception.
  private void testInvalidGrantRevoke(String resourceName, String canonicalResource,
      GrantRevokeStmt grantStmt, GrantRevokeStmt revokeStmt, List<String> allPermissions)
      throws Exception {

    // Grant all the permissions at once.
    String st = grantStmt.get("ALL", resourceName, role[0]);
    s.execute(st);
    assertPermissionsGranted(s, role[0], canonicalResource, allPermissions);

    // Test invalid statements.
    thrown.expect(SyntaxError.class);
    s.execute(grantStmt.get("INVALID_PERMISSION", resourceName, role[0]));

    thrown.expect(InvalidQueryException.class);
    s.execute(grantStmt.get("ALL", resourceName, "invalid_role"));
    assertPermissionsGranted(s, role[0], canonicalResource, allPermissions);

    if (!resourceName.isEmpty()) {
      thrown.expect(InvalidQueryException.class);
      s.execute(grantStmt.get("ALL", "invalid_resource", role[0]));
      assertPermissionsGranted(s, role[0], canonicalResource, allPermissions);
    }

    thrown.expect(SyntaxError.class);
    s.execute(revokeStmt.get("INVALID_PERMISSION", resourceName, role[0]));

    if (!resourceName.isEmpty()) {
      // This shouldn't return an error. It should just be ignored.
      s.execute(revokeStmt.get("ALL", "invalid_resource", role[0]));
      assertPermissionsGranted(s, role[0], canonicalResource, allPermissions);
    }

    // This shouldn't return an error. It should just be ignored.
    s.execute(revokeStmt.get("ALL", resourceName, "invalid_role"));
    assertPermissionsGranted(s, role[0], canonicalResource, allPermissions);
  }

  @Test
  public void testInvalidGrantRevokeTable() throws Exception {
    CreateRoles(1);

    String keyspace = "test_grant_revoke_table";
    createKeyspace(keyspace);
    useKeyspace(keyspace);

    String table = "table_test";
    createTable(table);
    String resourceName = String.format("%s.%s", keyspace, table);

    testInvalidGrantRevoke(resourceName, String.format("data/%s/%s", keyspace, table),
        (String permission, String resource, String role) ->
            GrantPermissionTableStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionTableStmt(permission, resource, role),
        ALL_PERMISSIONS_FOR_TABLE);
  }

  @Test
  public void testInvalidGrantRevokeRole() throws Exception {
    CreateRoles(2);

    testInvalidGrantRevoke(role[1], String.format("roles/%s", role[1]),
        (String permission, String resource, String role) ->
            GrantPermissionRoleStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionRoleStmt(permission, resource, role),
        ALL_PERMISSIONS_FOR_ROLE);
  }

  @Test
  public void testInvalidGrantRevokeOnAllRoles() throws Exception {
    CreateRoles(1);

    // Send an empty string as the resource name so the statements that use an invalid resource name
    // are not run because we ignore that parameter.
    testInvalidGrantRevoke("", "roles",
        (String permission, String resource, String role) ->
            GrantPermissionAllRolesStmt(permission, role),
        (String permission, String resource, String role) ->
            RevokePermissionAllRolesStmt(permission, role),
        ALL_PERMISSIONS_FOR_ALL_ROLES);
  }

  @Test
  public void testInvalidGrantRevokeOnAllKeyspaces() throws Exception {
    CreateRoles(1);

    // Create a few keyspaces
    for (int i = 0; i < 10; i++) {
      createKeyspace("test_grant_revoke_on_all_keyspaces_" + Integer.toString(i));
    }

    // Send an empty string as the resource name so the statements that use an invalid resource name
    // are not run because we ignore that parameter.
    testInvalidGrantRevoke("", "data",
        (String permission, String resource, String role) ->
            GrantPermissionAllKeyspacesStmt(permission, role),
        (String permission, String resource, String role) ->
            RevokePermissionAllKeyspacesStmt(permission, role),
        ALL_PERMISSIONS_FOR_ALL_KEYSPACES);
  }

  private void expectDescribeSyntaxError(String stmt) throws Exception {
    try {
      s.execute(stmt);
    } catch (SyntaxError e) {
      assert(e.toString().contains(DESCRIBE_SYNTAX_ERROR_MSG));
      return;
    } catch (Exception e) {
      throw e;
    }
    fail("Expected syntax error but execution succeeded");
  }

  @Test
  public void testDescribePermissions() throws Exception {
    CreateRoles(10);

    // Create a keyspace.
    String keyspace = "test_describe_permissions_keyspace";
    createKeyspace(keyspace);

    useKeyspace(keyspace);

    // Create a table.
    String table = "test_describe_permissions_table";
    createTable(table);

    final String DESCRIBE = "DESCRIBE";

    // Grant describe on a keyspace.
    expectDescribeSyntaxError(GrantPermissionKeyspaceStmt(DESCRIBE, keyspace, role[0]));

    // Revoke describe on a keyspace.
    expectDescribeSyntaxError(RevokePermissionKeyspaceStmt(DESCRIBE, keyspace, role[0]));

    // Grant describe on a keyspace.
    expectDescribeSyntaxError(GrantPermissionKeyspaceStmt(DESCRIBE, keyspace, role[0]));

    // Revoke describe on a keyspace.
    expectDescribeSyntaxError(RevokePermissionKeyspaceStmt(DESCRIBE, keyspace, role[0]));

    // Grant describe on a keyspace that doesn't exist. It should fail with a syntax error.
    expectDescribeSyntaxError(
        GrantPermissionKeyspaceStmt(DESCRIBE, "some_keyspace", role[0]));

    // Revoke describe on a keyspace that doesn't exist. It should fail with a syntax error.
    expectDescribeSyntaxError(
        RevokePermissionKeyspaceStmt(DESCRIBE, "some_keyspace", role[0]));

    // Grant describe on a kesypace to a role that doesn't exist.
    expectDescribeSyntaxError(GrantPermissionKeyspaceStmt(DESCRIBE, keyspace, "some_role"));

    // Revoke describe on a kesypace from a role that doesn't exist.
    expectDescribeSyntaxError(RevokePermissionKeyspaceStmt(DESCRIBE, keyspace, "some_role"));

    // Grant describe on a keyspace that doesn't exist. It should fail with a syntax error.
    expectDescribeSyntaxError(
        GrantPermissionKeyspaceStmt(DESCRIBE, "some_keyspace", role[0]));

    // Revoke describe on a keyspace that doesn't exist. It should fail with a syntax error.
    expectDescribeSyntaxError(
        RevokePermissionKeyspaceStmt(DESCRIBE, "some_keyspace", role[0]));

    // Grant describe on a keyspace that doesn't exist to a role that doesn't exist.
    expectDescribeSyntaxError(
        GrantPermissionKeyspaceStmt(DESCRIBE, "some_keyspace", "some_role"));

    // Revoke describe on a keyspace that doesn't exist from a role that doesn't exist.
    expectDescribeSyntaxError(
        RevokePermissionKeyspaceStmt(DESCRIBE, "some_keyspace", "some_role"));

    // Grant describe on all keyspaces.
    expectDescribeSyntaxError(GrantPermissionAllKeyspacesStmt(DESCRIBE, role[0]));

    // Revoke describe on all keyspaces.
    expectDescribeSyntaxError(RevokePermissionAllKeyspacesStmt(DESCRIBE, role[0]));

    // Grant describe on all keyspaces to a role that doesn't exist.
    expectDescribeSyntaxError(GrantPermissionAllKeyspacesStmt(DESCRIBE, "some_role"));

    // Revoke describe on all keyspaces from a role that doesn't exist.
    expectDescribeSyntaxError(RevokePermissionAllKeyspacesStmt(DESCRIBE, "some_role"));

    // Grant describe on a table.
    expectDescribeSyntaxError(GrantPermissionTableStmt(DESCRIBE, table, role[0]));

    // Revoke describe on a table.
    expectDescribeSyntaxError(RevokePermissionTableStmt(DESCRIBE, table, role[0]));

    // Grant describe on a table that doesn't exist.
    expectDescribeSyntaxError(GrantPermissionTableStmt(DESCRIBE, "some_table", role[0]));

    // Revoke describe on a table that doesn't exist.
    expectDescribeSyntaxError(RevokePermissionTableStmt(DESCRIBE, "some_table", role[0]));

    // Grant describe on a table to a role that doesn't exist.
    expectDescribeSyntaxError(GrantPermissionTableStmt(DESCRIBE, table, "some_role"));

    // Revoke describe on a table from a role that doesn't exist.
    expectDescribeSyntaxError(RevokePermissionTableStmt(DESCRIBE, table, "some_role"));

    // Grant describe on a table that doesn't exist to a role that doesn't exist.
    expectDescribeSyntaxError(
        GrantPermissionTableStmt(DESCRIBE, "some_table", "some_role"));

    // Revoke describe on a table that doesn't exist from a role that doesn't exist.
    expectDescribeSyntaxError(
        RevokePermissionTableStmt(DESCRIBE, "some_table", "some_role"));

    final List<String> DESCRIBE_LIST = Arrays.asList(DESCRIBE);

    // Grant describe on a role.
    String canonicalResource = "roles/" + role[2];
    expectDescribeSyntaxError(GrantPermissionRoleStmt(DESCRIBE, role[2], role[1]));

    // Revoke describe on a role.
    expectDescribeSyntaxError(RevokePermissionRoleStmt(DESCRIBE, role[2], role[1]));

    // Grant describe on all roles.
    canonicalResource = "roles";
    s.execute(GrantPermissionAllRolesStmt(DESCRIBE, role[3]));
    assertPermissionsGranted(s, role[3], canonicalResource, DESCRIBE_LIST);

    // Revoke describe on all roles.
    s.execute(RevokePermissionAllRolesStmt(DESCRIBE, role[3]));
    assertPermissionsGranted(s, role[3], canonicalResource, new ArrayList<>());

    final String ALL = "ALL";

    // Grant ALL on a role.
    canonicalResource = "roles/" + role[4];
    s.execute(GrantPermissionRoleStmt(ALL, role[4], role[5]));
    assertPermissionsGranted(s, role[5], canonicalResource, ALL_PERMISSIONS_FOR_ROLE);

    // Revoke ALL on a role.
    s.execute(RevokePermissionRoleStmt(ALL, role[4], role[5]));
    assertPermissionsGranted(s, role[5], canonicalResource, new ArrayList<>());

    // Grant ALL on all roles.
    canonicalResource = "roles";
    s.execute(GrantPermissionAllRolesStmt(ALL, role[6]));
    assertPermissionsGranted(s, role[6], canonicalResource, ALL_PERMISSIONS_FOR_ALL_ROLES);

    // Revoke ALL on all roles.
    s.execute(RevokePermissionAllRolesStmt(ALL, role[6]));
    assertPermissionsGranted(s, role[6], canonicalResource, new ArrayList<>());
  }
}
