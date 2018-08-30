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

import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;
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

import static junit.framework.TestCase.fail;
import static org.junit.Assert.assertEquals;

@RunWith(value=YBTestRunner.class)
public class TestRoles extends BaseAuthenticationCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestRoles.class);
  private String[] role = new String[10];

  // Permissions in the same order as in catalog_manager.cc.
  private static final List<String> p =
      Arrays.asList("ALTER", "DESCRIBE", "CREATE", "MODIFY", "DROP", "SELECT", "AUTHORIZE");

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
    Session s = getDefaultSession();
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

  private void assertPermissionsGranted(String role, String resource, List<String> permissions) {
    Session s = getDefaultSession();
    String stmt = String.format("SELECT permissions FROM system_auth.role_permissions " +
                                "WHERE role = '%s' and resource = '%s';", role, resource);
    List<Row> rows = s.execute(stmt).all();
    assertEquals(1, rows.size());

    List list = rows.get(0).getList("permissions", String.class);
    assertEquals(permissions.size(), list.size());

    for (String permission : permissions) {
      if (!list.contains(permission)) {
        fail("Unable to find permission " + permission);
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

    Session s = getDefaultSession();

    // Grant role[1] to role[0].
    s.execute(GrantStmt(role[1], role[0]));

    assertRoleGranted(role[0], Arrays.asList(role[1]));
  }

  @Test
  public void testGrantRoleCircularReference() throws Exception {
    // Create the roles.
    CreateRoles(4);

    Session s = getDefaultSession();

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

    Session s = getDefaultSession();

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

    Session s = getDefaultSession();

    // Try to grant role[0] to role[0]. It should fail.
    thrown.expect(InvalidQueryException.class);
    s.execute(GrantStmt(role[0], role[0]));
  }

  @Test
  public void testRevokeSeveralRoles() throws Exception {
    // Create the roles.
    CreateRoles(4);

    Session s = getDefaultSession();
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

    Session s = getDefaultSession();

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

    Session s = getDefaultSession();
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
     GrantRevokeStmt grantStmt, GrantRevokeStmt revokeStmt) throws Exception {

    Session s = getDefaultSession();

    s.execute(grantStmt.get(p.get(0), resourceName, role[0]));
    assertPermissionsGranted(role[0], canonicalResource, Arrays.asList(p.get(0)));

    s.execute(revokeStmt.get(p.get(0), resourceName, role[0]));
    assertPermissionsGranted(role[0], canonicalResource, Arrays.asList());

    // Grant all the permissions one by one.
    List<String> grantedPermissions = new ArrayList<>();
    for (String permission: p) {
      s.execute(grantStmt.get(permission, resourceName, role[0]));
      grantedPermissions.add(permission);
      assertPermissionsGranted(role[0], canonicalResource, grantedPermissions);
    }

    // Revoke all the permissions one by one.
    for (String permission: p) {
      LOG.info("Revoking permission " + permission);
      s.execute(revokeStmt.get(permission, resourceName, role[0]));
      grantedPermissions.remove(0);
      assertPermissionsGranted(role[0], canonicalResource, grantedPermissions);
    }

    // Grant all the permissions at once.
    s.execute(grantStmt.get("ALL", resourceName, role[0]));
    assertPermissionsGranted(role[0], canonicalResource, p);

    // Revoke all the permissions at once.
    s.execute(revokeStmt.get("ALL", resourceName, role[0]));
    assertPermissionsGranted(role[0], canonicalResource, Arrays.asList());

    // Grant all the permissions at once.
    s.execute(grantStmt.get("ALL", resourceName, role[0]));
    assertPermissionsGranted(role[0], canonicalResource, p);

    // Revoke one of the permissions in the middle.
    s.execute(revokeStmt.get("CREATE", resourceName, role[0]));
    List<String> permissions = new ArrayList<>();
    permissions.addAll(p);
    permissions.remove(2);
    assertPermissionsGranted(role[0], canonicalResource, permissions);

    // Revoke another permission in the middle.
    s.execute(revokeStmt.get("DESCRIBE", resourceName, role[0]));
    permissions.remove(1);
    assertPermissionsGranted(role[0], canonicalResource, permissions);

    // Revoke the first permission.
    s.execute(revokeStmt.get("ALTER", resourceName, role[0]));
    permissions.remove(0);
    assertPermissionsGranted(role[0], canonicalResource, permissions);

    // Revoke the last permission.
    s.execute(revokeStmt.get("AUTHORIZE", resourceName, role[0]));
    permissions.remove(permissions.size() - 1);
    assertPermissionsGranted(role[0], canonicalResource, permissions);
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
            RevokePermissionKeyspaceStmt(permission, resource, role));
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
            RevokePermissionTableStmt(permission, resource, role));
  }

  @Test
  public void testGrantRevokeRole() throws Exception {
    CreateRoles(2);

    testGrantRevoke(role[1], String.format("roles/%s", role[1]),
        (String permission, String resource, String role) ->
            GrantPermissionRoleStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionRoleStmt(permission, resource, role));
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
            RevokePermissionAllRolesStmt(permission, role));
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
            RevokePermissionAllKeyspacesStmt(permission, role));
  }

  // If the statement is "ON ALL KEYSPACES" or "ON ALL ROLES" resource name should be an empty
  // string so we don't run a statement with an invalid resource since it will be ignored and will
  // not throw the expected exception.
  private void testInvalidGrantRevoke(String resourceName, String canonicalResource,
      GrantRevokeStmt grantStmt, GrantRevokeStmt revokeStmt) throws Exception {

    Session s = getDefaultSession();
    // Grant all the permissions at once.
    s.execute(grantStmt.get("ALL", resourceName, role[0]));
    assertPermissionsGranted(role[0], canonicalResource, p);

    // Test invalid statements.
    thrown.expect(SyntaxError.class);
    s.execute(grantStmt.get("INVALID_PERMISSION", resourceName, role[0]));

    thrown.expect(InvalidQueryException.class);
    s.execute(grantStmt.get("ALL", resourceName, "invalid_role"));
    assertPermissionsGranted(role[0], canonicalResource, p);

    if (!resourceName.isEmpty()) {
      thrown.expect(InvalidQueryException.class);
      s.execute(grantStmt.get("ALL", "invalid_resource", role[0]));
      assertPermissionsGranted(role[0], canonicalResource, p);
    }

    thrown.expect(SyntaxError.class);
    s.execute(revokeStmt.get("INVALID_PERMISSION", resourceName, role[0]));

    if (!resourceName.isEmpty()) {
      // This shouldn't return an error. It should just be ignored.
      s.execute(revokeStmt.get("ALL", "invalid_resource", role[0]));
      assertPermissionsGranted(role[0], canonicalResource, p);
    }

    // This shouldn't return an error. It should just be ignored.
    s.execute(revokeStmt.get("ALL", resourceName, "invalid_role"));
    assertPermissionsGranted(role[0], canonicalResource, p);
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
            RevokePermissionTableStmt(permission, resource, role));
  }

  @Test
  public void testInvalidGrantRevokeRole() throws Exception {
    CreateRoles(2);

    testInvalidGrantRevoke(role[1], String.format("roles/%s", role[1]),
        (String permission, String resource, String role) ->
            GrantPermissionRoleStmt(permission, resource, role),
        (String permission, String resource, String role) ->
            RevokePermissionRoleStmt(permission, resource, role));
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
            RevokePermissionAllRolesStmt(permission, role));
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
            RevokePermissionAllKeyspacesStmt(permission, role));
  }

}
