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
package org.yb.cql;

import static org.yb.AssertionWrappers.*;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestName;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.exceptions.UnauthorizedException;
import com.google.common.io.Closeables;

// Shared fixture for the YCQL authorization-enforcement suites. CI runs every Java test method as
// a separate unit with its own mini-cluster, so a subclass must not inherit tests it does not
// need: the only tests here are the TRUNCATE ones whose outcome is the same with and without
// ycql_require_drop_privs_for_truncate, so both suites run them.
public abstract class BaseAuthorizationEnforcementTest extends BaseAuthenticationCQLTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(BaseAuthorizationEnforcementTest.class);

  protected static final long PERMISSIONS_CACHE_TIME_MS = 100;

  // Time to sleep. Used to give the clients enough time to update their permissions cache.
  // Used only when revoking a permission or altering the role to remove superuser property.
  protected static final long TIME_SLEEP_MS = PERMISSIONS_CACHE_TIME_MS * 4;

  // Value that we insert into the table.
  protected static final int VALUE = 5;

   // Session using 'cassandra' role.
  protected ClusterAndSession cs;

  // Session using the created role.
  protected ClusterAndSession cs2;

  protected String username;
  protected String anotherUsername;
  protected String password;
  protected String keyspace;
  protected String anotherKeyspace;
  protected String table;
  protected String anotherTable;

  @Rule
  public TestName testName = new TestName();

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("update_permissions_cache_msecs", String.valueOf(PERMISSIONS_CACHE_TIME_MS));
    return flagMap;
  }

  @Before
  public void setupAuth() throws Exception {
    cs = connectWithTestDefaults();

    String methodName = testName.getMethodName();
    String suffix = Integer.toString(Math.abs(methodName.hashCode()));

    username = "role_" + suffix;
    password = "password_"+ suffix;
    createRole(cs.getSession(), username, password, true, false, false);

    cs2 = connectWithCredentials(username, password);

    keyspace = "keyspace_" + suffix;
    table = "table_" + suffix;

    cs.execute("CREATE KEYSPACE " + keyspace);

    anotherUsername = username + "_2";
    anotherKeyspace = keyspace + "_2";
    anotherTable = table + "_2";

    if (methodName.startsWith("testGrantPermission") ||
        methodName.startsWith("testRevokePermission") ||
        methodName.startsWith("testGrantRole") ||
        methodName.startsWith("testRevokeRole")) {
      createRole(cs.getSession(), anotherUsername, password, true, false, false);
    }
  }

  @After
  public void cleanupAuth() throws Exception {
    Closeables.close(cs, false);
    Closeables.close(cs2, false);
  }

  protected List<String> getAllPermissionsExcept(List<String> exceptions) {
    List<String> permissions = new ArrayList<String>(ALL_PERMISSIONS);
    permissions.removeAll(exceptions);
    return permissions;
  }

  protected void revokePermissionNoSleep(String permission, String resourceType, String resource,
                                       String role) throws Exception {
    cs.execute(String.format("REVOKE %s ON %s %s FROM %s",
        permission, resourceType, resource, role));
  }

  protected void revokePermission(String permission, String resourceType, String resource,
                                  String role) throws Exception {
    revokePermissionNoSleep(permission, resourceType, resource, role);
    Thread.sleep(TIME_SLEEP_MS);
  }

  protected void grantPermission(String permission, String resourceType, String resource,
                                 String role) throws Exception {
    grantPermission(cs.getSession(), permission, resourceType, resource, role);
  }

  protected void grantAllPermissionsExcept(List<String> exceptions, String resourceType,
                                           String resource, String role) throws Exception {
    List<String> permissions = getAllPermissionsExcept(exceptions);
    for (String permission : permissions) {
      grantPermission(permission, resourceType, resource, role);
    }
  }

  protected void grantAllPermission(String resourceType, String resource, String role)
      throws Exception {
    grantPermission(ALL, resourceType, resource, role);
  }

  protected void grantPermissionOnAllKeyspaces(String permission, String role) throws Exception {
    grantPermission(permission, ALL_KEYSPACES, "", role);
  }

  protected void revokePermissionOnAllKeyspaces(String permission, String role) throws Exception {
    revokePermission(permission, ALL_KEYSPACES, "", role);
  }

  protected void grantPermissionOnAllRoles(String permission, String role) throws Exception {
    grantPermission(permission, ALL_ROLES, "", role);
  }

  protected void verifySomePermissionsGranted(String role, String resource) {
    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_auth.role_permissions WHERE role = '%s' AND resource = '%s'",
        role, resource));
    assert(!rs.all().isEmpty());
  }

  protected void verifyPermissionsDeleted(String role, String resource) {
    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_auth.role_permissions WHERE role = '%s' AND resource = '%s'",
        role, resource));
    assert(rs.all().isEmpty());
  }

  protected void verifyKeyspaceExists(String keyspaceName) throws Exception {
    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_schema.keyspaces WHERE keyspace_name = '%s'", keyspaceName));
    List<Row> list = rs.all();
    assertEquals(1, list.size());
  }

  protected void createKeyspaceAndVerify(Session session, String keyspaceName) throws Exception {
    session.execute("CREATE KEYSPACE " + keyspaceName);
    verifyKeyspaceExists(keyspaceName);
  }

  protected void deleteKeyspaceAndVerify(Session session, String keyspaceName) throws Exception {
    verifyKeyspaceExists(keyspaceName);

    session.execute("DROP KEYSPACE " + keyspaceName);

    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_schema.keyspaces WHERE keyspace_name = '%s'", keyspaceName));
    List<Row> list = rs.all();
    assertEquals(0, list.size());
  }

  protected void verifyTableExists(String keyspaceName, String tableName) {
    // Verify that the table was created.
    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_schema.tables WHERE keyspace_name = '%s' AND table_name = '%s'",
        keyspaceName, tableName));

    List<Row> list = rs.all();
    assertEquals(1, list.size());
  }

  protected void createTableAndVerify(Session session, String keyspaceName, String tableName)
      throws Exception {
    // Now, username should be able to create the table.
    session.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h))",
        keyspaceName, tableName));

    cs.execute("USE " + keyspaceName);
    verifyTableExists(keyspaceName, tableName);
  }

  protected void deleteTableAndVerify(Session session, String keyspaceName, String tableName)
    throws Exception {
    verifyTableExists(keyspaceName, tableName);
    session.execute(String.format("DROP TABLE %s.%s ", keyspaceName, tableName));

    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_schema.tables WHERE keyspace_name = '%s' AND table_name = '%s'",
        keyspaceName, tableName));

    List<Row> list = rs.all();
    assertEquals(0, list.size());
  }

  protected void verifyRow(Session session, String keyspaceName, String tableName,
                           int expectedValue) throws Exception {

    ResultSet rs = session.execute(String.format("SELECT * FROM %s.%s", keyspaceName, table));
    List<Row> rows = rs.all();
    assertEquals(1, rows.size());
    assertEquals(VALUE, rows.get(0).getInt("h"));
    assertEquals(expectedValue, rows.get(0).getInt("v"));
  }

  protected void selectAndVerify(Session session, String keyspaceName, String tableName)
    throws Exception {
    verifyRow(session, keyspaceName, tableName, VALUE);
  }

  protected void insertRow(Session session, String keyspaceName, String tableName)
    throws Exception {

    session.execute(String.format("INSERT INTO %s.%s (h, v) VALUES (%d, %d)",
        keyspaceName, tableName, VALUE, VALUE));

    // We always verify by using the cassandra role.
    selectAndVerify(cs.getSession(), keyspaceName, tableName);
  }

  protected void updateRowAndVerify(Session session, String keyspaceName, String tableName)
    throws Exception {

    session.execute(String.format("UPDATE %s.%s SET v = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));

    verifyRow(cs.getSession(), keyspaceName, tableName, VALUE + 1);
  }

  protected void truncateTableAndVerify(Session session, String keyspaceName, String tableName)
      throws Exception {
    cs2.execute(String.format("TRUNCATE %s.%s", keyspaceName, tableName));

    ResultSet rs = cs.execute(String.format("SELECT * FROM %s.%s", keyspaceName, tableName));
    assertEquals(0, rs.all().size());
  }

  protected void createTableAndInsertRecord(Session session, String keyspaceName, String tableName)
      throws Exception {
    createTableAndVerify(session, keyspaceName, tableName);
    insertRow(session, keyspaceName, tableName);
  }

  // The five TRUNCATE tests below run in both final suites, TestAuthorizationEnforcement and
  // TestAuthorizationEnforcementWithTruncate. None of them gives the test role DROP or MODIFY on
  // the target table or its keyspace, so TRUNCATE must be rejected whether or not
  // ycql_require_drop_privs_for_truncate is set.
  @Test
  public void testTruncateStatementWithNoPermissions() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithModifyPermissionOnDifferentTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createTableAndInsertRecord(cs.getSession(), keyspace, anotherTable);
    grantPermission(MODIFY, TABLE, anotherTable, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithModifyPermissionOnDifferentKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createKeyspaceAndVerify(cs.getSession(), anotherKeyspace);
    grantPermission(MODIFY, KEYSPACE, anotherKeyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStamentWithModifyPermissionOnTableToDifferentRole() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(MODIFY, TABLE, table, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithModifyPermissionOnKeyspaceToDifferentRole()
      throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(MODIFY, KEYSPACE, keyspace, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }
}
