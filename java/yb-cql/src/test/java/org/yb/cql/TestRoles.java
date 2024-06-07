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

import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.InvalidQueryException;
import com.datastax.driver.core.exceptions.SyntaxError;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.HashMap;


@RunWith(value=YBTestRunner.class)
public class TestRoles extends BaseAuthenticationCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestRoles.class);

  // Permissions in the same order as in catalog_manager.cc.
  private static final List<String> ALL_PERMISSIONS =
      Arrays.asList("ALTER", "AUTHORIZE", "CREATE", "DESCRIBE", "DROP", "MODIFY", "SELECT");

  private static final List<String> ALL_PERMISSIONS_EXCEPT_DESCRIBE =
      Arrays.asList("ALTER", "AUTHORIZE", "CREATE", "DROP", "MODIFY", "SELECT");

  private static final String DESCRIBE_SYNTAX_ERROR_MSG =
      "Resource type DataResource does not support any of the requested permissions";

  private List<String> createRoles(int n) throws Exception {
    StackTraceElement[] stackTraceElements = Thread.currentThread().getStackTrace();
    String callersName = stackTraceElements[2].getMethodName().toLowerCase();
    List<String> roleNames = new ArrayList<>();
    for (int i = 0; i < n; i++) {
      String roleName = String.format("role_%d_%s", i, callersName);
      LOG.info("role name: " + roleName);
      String password = ";oqr94t";
      createRole(session, roleName, password, true, false, true);
      roleNames.add(roleName);
    }
    return roleNames;
  }

  private void assertRoleGranted(String role, List<String> memberOf) {
    List<Row> rows = session.execute(
        String.format("SELECT * FROM system_auth.roles WHERE role = '%s';", role)).all();
    assertEquals(1, rows.size());

    List<String> list = rows.get(0).getList("member_of", String.class);
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

  @Test
  public void testGrantRole() throws Exception {
    // Create the roles.
    List<String> roles = createRoles(2);

    // Grant roles.get(1) to roles.get(0).
    session.execute(GrantStmt(roles.get(1), roles.get(0)));

    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(1)));
  }

  @Test
  public void testGrantRoleCircularReference() throws Exception {
    // Create the roles.
    List<String> roles = createRoles(4);

    // Grant roles.get(1) to roles.get(0).
    session.execute(GrantStmt(roles.get(1), roles.get(0)));
    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(1)));

    // Try to grant roles.get(0) to roles.get(1) to create a circular reference. It should fail.
    thrown.expect(InvalidQueryException.class);
    thrown.expectMessage(String.format("%s is a member of %s", roles.get(1), roles.get(0)));
    session.execute(GrantStmt(roles.get(0), roles.get(1)));

    assertRoleGranted(roles.get(1), Arrays.asList());

    // Grant roles.get(2) to roles.get(1).
    session.execute(GrantStmt(roles.get(2), roles.get(1)));
    assertRoleGranted(roles.get(1), Arrays.asList(roles.get(2)));

    // Try to grant roles.get(2) to roles.get(0) to create a circular reference. It should fail.
    thrown.expect(InvalidQueryException.class);
    thrown.expectMessage(String.format("%s is a member of %s", roles.get(0), roles.get(2)));
    session.execute(GrantStmt(roles.get(2), roles.get(0)));
    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(1)));

    // Try to grant roles.get(0) to roles.get(2) to create a circular reference. It should fail.
    thrown.expect(InvalidQueryException.class);
    thrown.expectMessage(String.format("%s is a member of %s", roles.get(2), roles.get(0)));
    session.execute(GrantStmt(roles.get(0), roles.get(2)));
    assertRoleGranted(roles.get(2), Arrays.asList());
  }

  @Test
  public void testGrantInvalidRole() throws Exception {
    // Create one role.
    List<String> roles = createRoles(1);

    // Try to grant a role that doesn't exist to roles.get(0).
    String invalidRole = "invalid_role";
    thrown.expect(InvalidQueryException.class);
    thrown.expectMessage(String.format("%s doesn't exist", invalidRole));
    session.execute(GrantStmt(invalidRole, roles.get(0)));
  }

  @Test
  public void testCannotGrantRoleToItself() throws Exception {
    // Create a role.
    List<String> roles = createRoles(1);

    // Try to grant roles.get(0) to roles.get(0). It should fail.
    thrown.expect(InvalidQueryException.class);
    session.execute(GrantStmt(roles.get(0), roles.get(0)));
  }

  @Test
  public void testRevokeSeveralRoles() throws Exception {
    // Create the roles.
    List<String> roles = createRoles(4);

    session.execute(GrantStmt(roles.get(1), roles.get(0)));
    session.execute(GrantStmt(roles.get(2), roles.get(0)));
    session.execute(GrantStmt(roles.get(3), roles.get(0)));
    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(1), roles.get(2), roles.get(3)));

    session.execute(RevokeStmt(roles.get(2), roles.get(0)));
    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(1), roles.get(3)));

    session.execute(RevokeStmt(roles.get(1), roles.get(0)));
    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(3)));
  }

  @Test
  public void testRevokeRoleFromItself() throws Exception {
    // Create the roles.
    List<String> roles = createRoles(2);

    // Grant roles.get(1) to roles.get(0).
    session.execute(GrantStmt(roles.get(1), roles.get(0)));
    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(1)));

    // Try to revoke roles.get(0) from roles.get(0). It should return without an error although
    // nothing will happen.
    session.execute(RevokeStmt(roles.get(0), roles.get(0)));

    // Verify that role0's member_of field didn't get modified.
    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(1)));
  }

  @Test
  public void testRevokeRole() throws Exception {
    // Create the roles.
    List<String> roles = createRoles(2);

    session.execute(GrantStmt(roles.get(1), roles.get(0)));
    assertRoleGranted(roles.get(0), Arrays.asList(roles.get(1)));

    // Revoke roles.get(1) from roles.get(0).
    session.execute(RevokeStmt(roles.get(1), roles.get(0)));
    assertRoleGranted(roles.get(0), Arrays.asList());
  }

  public interface GrantRevokeStmt {
    String get(String permission, String resource, String role) throws Exception;
  }

  private void testGrantRevoke(List<String> roles, String resourceName, String canonicalResource,
      GrantRevokeStmt grantStmt, GrantRevokeStmt revokeStmt, List<String> allPermissions)
      throws Exception {

    session.execute(grantStmt.get(allPermissions.get(0), resourceName, roles.get(0)));
    assertPermissionsGranted(session, roles.get(0), canonicalResource,
        Arrays.asList(allPermissions.get(0)));

    session.execute(revokeStmt.get(allPermissions.get(0), resourceName, roles.get(0)));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, Arrays.asList());

    // Grant all the permissions one by one.
    List<String> grantedPermissions = new ArrayList<>();
    for (String permission: allPermissions) {
      session.execute(grantStmt.get(permission, resourceName, roles.get(0)));
      grantedPermissions.add(permission);
      assertPermissionsGranted(session, roles.get(0), canonicalResource, grantedPermissions);
    }

    // Revoke all the permissions one by one.
    for (String permission: allPermissions) {
      LOG.info("Revoking permission " + permission);
      session.execute(revokeStmt.get(permission, resourceName, roles.get(0)));
      grantedPermissions.remove(0);
      assertPermissionsGranted(session, roles.get(0), canonicalResource, grantedPermissions);
    }

    // Grant all the permissions at once.
    session.execute(grantStmt.get("ALL", resourceName, roles.get(0)));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, allPermissions);

    // Revoke all the permissions at once.
    session.execute(revokeStmt.get("ALL", resourceName, roles.get(0)));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, Arrays.asList());

    // Grant all the permissions at once.
    session.execute(grantStmt.get("ALL", resourceName, roles.get(0)));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, allPermissions);

    // Revoke one of the permissions in the middle.
    session.execute(revokeStmt.get("DROP", resourceName, roles.get(0)));
    List<String> permissions = new ArrayList<>();
    permissions.addAll(allPermissions);
    permissions.remove(permissions.indexOf("DROP"));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, permissions);

    if (permissions.contains("DESCRIBE")) {
      // Revoke another permission in the middle.
      session.execute(revokeStmt.get("DESCRIBE", resourceName, roles.get(0)));
      permissions.remove(permissions.indexOf("DESCRIBE"));
      assertPermissionsGranted(session, roles.get(0), canonicalResource, permissions);
    }

    // Revoke the first permission.
    session.execute(revokeStmt.get("ALTER", resourceName, roles.get(0)));
    permissions.remove(permissions.indexOf("ALTER"));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, permissions);

    // Revoke the last permission.
    session.execute(revokeStmt.get("AUTHORIZE", resourceName, roles.get(0)));
    permissions.remove(permissions.indexOf("AUTHORIZE"));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, permissions);
  }

  @Test
  public void testGrantRevokeKeyspace() throws Exception {
    List<String> roles = createRoles(1);

    String resourceName = "test_grant_revoke_keyspace";
    createKeyspace(resourceName);

    testGrantRevoke(roles, resourceName, "data/" + resourceName,
        (String permission, String resource, String role) ->
            GrantPermissionKeyspaceStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionKeyspaceStmt(permission, resource, role),
        ALL_PERMISSIONS_EXCEPT_DESCRIBE);
  }

  @Test
  public void testGrantRevokeTable() throws Exception {
    List<String> roles = createRoles(1);

    String keyspace = "test_grant_revoke_table";
    createKeyspace(keyspace);
    useKeyspace(keyspace);

    String table = "table_test";
    createTable(table);
    String resourceName = String.format("%s.%s", keyspace, table);

    testGrantRevoke(roles, resourceName, String.format("data/%s/%s", keyspace, table),
        (String permission, String resource, String role) ->
            GrantPermissionTableStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionTableStmt(permission, resource, role),
        ALL_PERMISSIONS_FOR_TABLE);
  }

  @Test
  public void testGrantRevokeRole() throws Exception {
    List<String> roles = createRoles(2);

    testGrantRevoke(roles, roles.get(1), String.format("roles/%s", roles.get(1)),
        (String permission, String resource, String role) ->
            GrantPermissionRoleStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionRoleStmt(permission, resource, role),
        ALL_PERMISSIONS_FOR_ROLE);
  }

  @Test
  public void testGrantRevokeOnAllRoles() throws Exception {
    List<String> roles = createRoles(1);

    // dummy_role will never be used because resource is ignored when we call
    // GrantPermissionAllRolesStmt and RevokePermissionAllRolesStmt.
    testGrantRevoke(roles, "dummy_role", "roles",
        (String permission, String resource, String role) ->
            GrantPermissionAllRolesStmt(permission, role),
        (String permission, String resource, String role) ->
            RevokePermissionAllRolesStmt(permission, role),
        ALL_PERMISSIONS_FOR_ALL_ROLES);
  }

  @Test
  public void testGrantRevokeOnAllKeyspaces() throws Exception {
    List<String> roles = createRoles(1);

    // Create a few keyspaces
    for (int i = 0; i < 10; i++) {
      createKeyspace("test_grant_revoke_on_all_keyspaces_" + Integer.toString(i));
    }

    // dummy_keyspace will never be used because resource is ignored when we call
    // GrantPermissionAllKeyspacesStmt and RevokePermissionAllKeyspacesStmt.
    testGrantRevoke(roles, "dummy_keyspace", "data",
        (String permission, String resource, String role) ->
            GrantPermissionAllKeyspacesStmt(permission, role),
        (String permission, String resource, String role) ->
            RevokePermissionAllKeyspacesStmt(permission, role),
        ALL_PERMISSIONS_FOR_ALL_KEYSPACES);
  }

  // If the statement is "ON ALL KEYSPACES" or "ON ALL ROLES" resource name should be an empty
  // string so we don't run a statement with an invalid resource since it will be ignored and will
  // not throw the expected exception.
  private void testInvalidGrantRevoke(List<String> roles, String resourceName,
      String canonicalResource, GrantRevokeStmt grantStmt, GrantRevokeStmt revokeStmt,
      List<String> allPermissions) throws Exception {

    // Grant all the permissions at once.
    String st = grantStmt.get("ALL", resourceName, roles.get(0));
    session.execute(st);
    assertPermissionsGranted(session, roles.get(0), canonicalResource, allPermissions);

    // Test invalid statements.
    thrown.expect(SyntaxError.class);
    session.execute(grantStmt.get("INVALID_PERMISSION", resourceName, roles.get(0)));

    thrown.expect(InvalidQueryException.class);
    session.execute(grantStmt.get("ALL", resourceName, "invalid_role"));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, allPermissions);

    if (!resourceName.isEmpty()) {
      thrown.expect(InvalidQueryException.class);
      session.execute(grantStmt.get("ALL", "invalid_resource", roles.get(0)));
      assertPermissionsGranted(session, roles.get(0), canonicalResource, allPermissions);
    }

    thrown.expect(SyntaxError.class);
    session.execute(revokeStmt.get("INVALID_PERMISSION", resourceName, roles.get(0)));

    if (!resourceName.isEmpty()) {
      // This shouldn't return an error. It should just be ignored.
      session.execute(revokeStmt.get("ALL", "invalid_resource", roles.get(0)));
      assertPermissionsGranted(session, roles.get(0), canonicalResource, allPermissions);
    }

    // This shouldn't return an error. It should just be ignored.
    session.execute(revokeStmt.get("ALL", resourceName, "invalid_role"));
    assertPermissionsGranted(session, roles.get(0), canonicalResource, allPermissions);
  }

  @Test
  public void testInvalidGrantRevokeTable() throws Exception {
    List<String> roles = createRoles(1);

    String keyspace = "test_grant_revoke_table";
    createKeyspace(keyspace);
    useKeyspace(keyspace);

    String table = "table_test";
    createTable(table);
    String resourceName = String.format("%s.%s", keyspace, table);

    testInvalidGrantRevoke(roles, resourceName, String.format("data/%s/%s", keyspace, table),
        (String permission, String resource, String role) ->
            GrantPermissionTableStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionTableStmt(permission, resource, role),
        ALL_PERMISSIONS_FOR_TABLE);
  }

  @Test
  public void testInvalidGrantRevokeRole() throws Exception {
    List<String> roles = createRoles(2);

    testInvalidGrantRevoke(roles, roles.get(1), String.format("roles/%s", roles.get(1)),
        (String permission, String resource, String role) ->
            GrantPermissionRoleStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionRoleStmt(permission, resource, role),
        ALL_PERMISSIONS_FOR_ROLE);
  }

  @Test
  public void testInvalidGrantRevokeOnAllRoles() throws Exception {
    List<String> roles = createRoles(1);

    // Send an empty string as the resource name so the statements that use an invalid resource name
    // are not run because we ignore that parameter.
    testInvalidGrantRevoke(roles, "", "roles",
        (String permission, String resource, String role) ->
            GrantPermissionAllRolesStmt(permission, role),
        (String permission, String resource, String role) ->
            RevokePermissionAllRolesStmt(permission, role),
        ALL_PERMISSIONS_FOR_ALL_ROLES);
  }

  @Test
  public void testInvalidGrantRevokeOnAllKeyspaces() throws Exception {
    List<String> roles = createRoles(1);

    // Create a few keyspaces
    for (int i = 0; i < 10; i++) {
      createKeyspace("test_grant_revoke_on_all_keyspaces_" + Integer.toString(i));
    }

    // Send an empty string as the resource name so the statements that use an invalid resource name
    // are not run because we ignore that parameter.
    testInvalidGrantRevoke(roles, "", "data",
        (String permission, String resource, String role) ->
            GrantPermissionAllKeyspacesStmt(permission, role),
        (String permission, String resource, String role) ->
            RevokePermissionAllKeyspacesStmt(permission, role),
        ALL_PERMISSIONS_FOR_ALL_KEYSPACES);
  }

  @Test
  public void testGrantRoleNoAccessToRecipient() throws Exception {
    createRole(session, "db_admin", "password", true, false, true);
    createRole(session, "db_reader", "pwd", false, false, false);
    createRole(session, "user", "pwd", false, false, false);

    // SuperUser can grant/revoke any role.
    session.execute(GrantStmt("db_reader", "user"));
    session.execute(RevokeStmt("db_reader", "user"));

    // Connect as 'db_admin'.
    try (ClusterAndSession cs = connectWithCredentials("db_admin", "password")) {
      // AUTHORIZE permission was not granted.
      thrown.expect(com.datastax.driver.core.exceptions.UnauthorizedException.class);
      thrown.expectMessage("Unauthorized. User db_admin does not have sufficient privileges " +
                           "to perform the requested operation");
      cs.execute(GrantStmt("db_reader", "user"));
    }

    session.execute(GrantPermissionRoleStmt("AUTHORIZE", "db_reader", "db_admin"));
    try (ClusterAndSession cs = connectWithCredentials("db_admin", "password")) {
      // Access to 'user' is not required.
      cs.execute(GrantStmt("db_reader", "user"));
      cs.execute(RevokeStmt("db_reader", "user"));
    }
  }

  private void expectDescribeSyntaxError(String stmt) throws Exception {
    try {
      session.execute(stmt);
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
    List<String> roles = createRoles(10);

    // Create a keyspace.
    String keyspace = "test_describe_permissions_keyspace";
    createKeyspace(keyspace);

    useKeyspace(keyspace);

    // Create a table.
    String table = "test_describe_permissions_table";
    createTable(table);

    final String DESCRIBE = "DESCRIBE";

    // Grant describe on a keyspace.
    expectDescribeSyntaxError(GrantPermissionKeyspaceStmt(DESCRIBE, keyspace, roles.get(0)));

    // Revoke describe on a keyspace.
    expectDescribeSyntaxError(RevokePermissionKeyspaceStmt(DESCRIBE, keyspace, roles.get(0)));

    // Grant describe on a keyspace.
    expectDescribeSyntaxError(GrantPermissionKeyspaceStmt(DESCRIBE, keyspace, roles.get(0)));

    // Revoke describe on a keyspace.
    expectDescribeSyntaxError(RevokePermissionKeyspaceStmt(DESCRIBE, keyspace, roles.get(0)));

    // Grant describe on a keyspace that doesn't exist. It should fail with a syntax error.
    expectDescribeSyntaxError(
        GrantPermissionKeyspaceStmt(DESCRIBE, "some_keyspace", roles.get(0)));

    // Revoke describe on a keyspace that doesn't exist. It should fail with a syntax error.
    expectDescribeSyntaxError(
        RevokePermissionKeyspaceStmt(DESCRIBE, "some_keyspace", roles.get(0)));

    // Grant describe on a kesypace to a role that doesn't exist.
    expectDescribeSyntaxError(GrantPermissionKeyspaceStmt(DESCRIBE, keyspace, "some_role"));

    // Revoke describe on a kesypace from a role that doesn't exist.
    expectDescribeSyntaxError(RevokePermissionKeyspaceStmt(DESCRIBE, keyspace, "some_role"));

    // Grant describe on a keyspace that doesn't exist. It should fail with a syntax error.
    expectDescribeSyntaxError(
        GrantPermissionKeyspaceStmt(DESCRIBE, "some_keyspace", roles.get(0)));

    // Revoke describe on a keyspace that doesn't exist. It should fail with a syntax error.
    expectDescribeSyntaxError(
        RevokePermissionKeyspaceStmt(DESCRIBE, "some_keyspace", roles.get(0)));

    // Grant describe on a keyspace that doesn't exist to a role that doesn't exist.
    expectDescribeSyntaxError(
        GrantPermissionKeyspaceStmt(DESCRIBE, "some_keyspace", "some_role"));

    // Revoke describe on a keyspace that doesn't exist from a role that doesn't exist.
    expectDescribeSyntaxError(
        RevokePermissionKeyspaceStmt(DESCRIBE, "some_keyspace", "some_role"));

    // Grant describe on all keyspaces.
    expectDescribeSyntaxError(GrantPermissionAllKeyspacesStmt(DESCRIBE, roles.get(0)));

    // Revoke describe on all keyspaces.
    expectDescribeSyntaxError(RevokePermissionAllKeyspacesStmt(DESCRIBE, roles.get(0)));

    // Grant describe on all keyspaces to a role that doesn't exist.
    expectDescribeSyntaxError(GrantPermissionAllKeyspacesStmt(DESCRIBE, "some_role"));

    // Revoke describe on all keyspaces from a role that doesn't exist.
    expectDescribeSyntaxError(RevokePermissionAllKeyspacesStmt(DESCRIBE, "some_role"));

    // Grant describe on a table.
    expectDescribeSyntaxError(GrantPermissionTableStmt(DESCRIBE, table, roles.get(0)));

    // Revoke describe on a table.
    expectDescribeSyntaxError(RevokePermissionTableStmt(DESCRIBE, table, roles.get(0)));

    // Grant describe on a table that doesn't exist.
    expectDescribeSyntaxError(GrantPermissionTableStmt(DESCRIBE, "some_table", roles.get(0)));

    // Revoke describe on a table that doesn't exist.
    expectDescribeSyntaxError(RevokePermissionTableStmt(DESCRIBE, "some_table", roles.get(0)));

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
    String canonicalResource = "roles/" + roles.get(2);
    expectDescribeSyntaxError(GrantPermissionRoleStmt(DESCRIBE, roles.get(2), roles.get(1)));

    // Revoke describe on a role.
    expectDescribeSyntaxError(RevokePermissionRoleStmt(DESCRIBE, roles.get(2), roles.get(1)));

    // Grant describe on all roles.
    canonicalResource = "roles";
    session.execute(GrantPermissionAllRolesStmt(DESCRIBE, roles.get(3)));
    assertPermissionsGranted(session, roles.get(3), canonicalResource, DESCRIBE_LIST);

    // Revoke describe on all roles.
    session.execute(RevokePermissionAllRolesStmt(DESCRIBE, roles.get(3)));
    assertPermissionsGranted(session, roles.get(3), canonicalResource, new ArrayList<>());

    final String ALL = "ALL";

    // Grant ALL on a role.
    canonicalResource = "roles/" + roles.get(4);
    session.execute(GrantPermissionRoleStmt(ALL, roles.get(4), roles.get(5)));
    assertPermissionsGranted(session, roles.get(5), canonicalResource, ALL_PERMISSIONS_FOR_ROLE);

    // Revoke ALL on a role.
    session.execute(RevokePermissionRoleStmt(ALL, roles.get(4), roles.get(5)));
    assertPermissionsGranted(session, roles.get(5), canonicalResource, new ArrayList<>());

    // Grant ALL on all roles.
    canonicalResource = "roles";
    session.execute(GrantPermissionAllRolesStmt(ALL, roles.get(6)));
    assertPermissionsGranted(session, roles.get(6), canonicalResource,
        ALL_PERMISSIONS_FOR_ALL_ROLES);

    // Revoke ALL on all roles.
    session.execute(RevokePermissionAllRolesStmt(ALL, roles.get(6)));
    assertPermissionsGranted(session, roles.get(6), canonicalResource, new ArrayList<>());
  }

  @Test
  public void testCassandraUserRecreationDisabledOnRestart() throws Exception {
    String cassandra_user = "cassandra";
    String superuser2 = "superuser2";
    String password = "password";

    // Create a new 'superuser2' role (as 'cassandra')
    createRole(session, superuser2, password, true, true, true);

    // Connect as 'superuser2'.
    try (ClusterAndSession cs2 = connectWithCredentials(superuser2, password)) {
    // Verify that second_admin can delete 'cassandra'.
      cs2.execute(String.format("DROP ROLE %s", cassandra_user));
    }

    // Verify that we can't connect using the deleted 'cassandra' role.
    checkConnectivity(true, cassandra_user, cassandra_user, true);

    // Restart the cluster
    restartYcqlMiniCluster();

    // Verify again after restart that we can't connect using the deleted 'cassandra' role.
    checkConnectivity(true, cassandra_user, cassandra_user, true);

    // Restart cluster with autoflag disabled
    Map<String, String> flags = new HashMap<>();
    flags.put("ycql_allow_cassandra_drop", "false");
    restartClusterWithMasterFlags(flags);

    // Verify that we can connect as cassandra role as it gets regenerated with flag disabled
    checkConnectivity(true, cassandra_user, cassandra_user, false);
    markClusterNeedsRecreation();
  }
}
