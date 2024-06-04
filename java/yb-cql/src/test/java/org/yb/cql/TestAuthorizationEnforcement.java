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

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestName;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.exceptions.SyntaxError;
import com.datastax.driver.core.exceptions.UnauthorizedException;
import com.google.common.io.Closeables;

@RunWith(value=YBTestRunner.class)
public class TestAuthorizationEnforcement extends BaseAuthenticationCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(org.yb.cql.TestRoles.class);

  private static final long PERMISSIONS_CACHE_TIME_MS = 100;

  // Time to sleep. Used to give the clients enough time to update their permissions cache.
  // Used only when revoking a permission or altering the role to remove superuser property.
  private static final long TIME_SLEEP_MS = PERMISSIONS_CACHE_TIME_MS * 4;

  // Value that we insert into the table.
  private static final int VALUE = 5;

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

  private List<String> getAllPermissionsExcept(List<String> exceptions) {
    List<String> permissions = new ArrayList<String>(ALL_PERMISSIONS);
    permissions.removeAll(exceptions);
    return permissions;
  }

  private void revokePermissionNoSleep(String permission, String resourceType, String resource,
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

  private void grantPermissionOnAllKeyspaces(String permission, String role) throws Exception {
    grantPermission(permission, ALL_KEYSPACES, "", role);
  }

  protected void revokePermissionOnAllKeyspaces(String permission, String role) throws Exception {
    revokePermission(permission, ALL_KEYSPACES, "", role);
  }

  private void grantPermissionOnAllRoles(String permission, String role) throws Exception {
    grantPermission(permission, ALL_ROLES, "", role);
  }

  private void verifySomePermissionsGranted(String role, String resource) {
    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_auth.role_permissions WHERE role = '%s' AND resource = '%s'",
        role, resource));
    assert(!rs.all().isEmpty());
  }

  private void verifyPermissionsDeleted(String role, String resource) {
    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_auth.role_permissions WHERE role = '%s' AND resource = '%s'",
        role, resource));
    assert(rs.all().isEmpty());
  }

  private void verifyKeyspaceExists(String keyspaceName) throws Exception {
    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_schema.keyspaces WHERE keyspace_name = '%s'", keyspaceName));
    List<Row> list = rs.all();
    assertEquals(1, list.size());
  }

  protected void createKeyspaceAndVerify(Session session, String keyspaceName) throws Exception {
    session.execute("CREATE KEYSPACE " + keyspaceName);
    verifyKeyspaceExists(keyspaceName);
  }

  private void deleteKeyspaceAndVerify(Session session, String keyspaceName) throws Exception {
    verifyKeyspaceExists(keyspaceName);

    session.execute("DROP KEYSPACE " + keyspaceName);

    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_schema.keyspaces WHERE keyspace_name = '%s'", keyspaceName));
    List<Row> list = rs.all();
    assertEquals(0, list.size());
  }

  private void verifyTableExists(String keyspaceName, String tableName) {
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

  private void deleteTableAndVerify(Session session, String keyspaceName, String tableName)
    throws Exception {
    verifyTableExists(keyspaceName, tableName);
    session.execute(String.format("DROP TABLE %s.%s ", keyspaceName, tableName));

    ResultSet rs = cs.execute(String.format(
        "SELECT * FROM system_schema.tables WHERE keyspace_name = '%s' AND table_name = '%s'",
        keyspaceName, tableName));

    List<Row> list = rs.all();
    assertEquals(0, list.size());
  }

  private void verifyRow(Session session, String keyspaceName, String tableName, int expectedValue)
      throws Exception {

    ResultSet rs = session.execute(String.format("SELECT * FROM %s.%s", keyspaceName, table));
    List<Row> rows = rs.all();
    assertEquals(1, rows.size());
    assertEquals(VALUE, rows.get(0).getInt("h"));
    assertEquals(expectedValue, rows.get(0).getInt("v"));
  }

  private void selectAndVerify(Session session, String keyspaceName, String tableName)
    throws Exception {
    verifyRow(session, keyspaceName, tableName, VALUE);
  }

  private void insertRow(Session session, String keyspaceName, String tableName)
    throws Exception {

    session.execute(String.format("INSERT INTO %s.%s (h, v) VALUES (%d, %d)",
        keyspaceName, tableName, VALUE, VALUE));

    // We always verify by using the cassandra role.
    selectAndVerify(cs.getSession(), keyspaceName, tableName);
  }

  private void updateRowAndVerify(Session session, String keyspaceName, String tableName)
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

  @Test
  public void testCreateKeyspaceWithoutPermissions() throws Exception {
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("CREATE KEYSPACE %s_2", keyspace));
  }

  @Test
  public void testCreateKeyspaceWithWrongPermissions() throws Exception {
    // Grant all the permissions except CREATE.
    grantAllPermissionsExcept(Arrays.asList(CREATE, DESCRIBE, AUTHORIZE),
        ALL_KEYSPACES, "", username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("CREATE KEYSPACE %s_2", keyspace));
  }

  @Test
  public void testCreateKeyspaceWithCreatePermission() throws Exception {
    // Grant CREATE permission.
    grantPermissionOnAllKeyspaces(CREATE, username);

    createKeyspaceAndVerify(cs2.getSession(), keyspace + "_2");
  }

  @Test
  public void testCreateKeyspaceWithAllPermissions() throws Exception {
    // Grant ALL permissions.
    grantPermissionOnAllKeyspaces(ALL, username);

    createKeyspaceAndVerify(cs2.getSession(), keyspace + "_2");
  }

  @Test
  public void testSuperuserCanCreateKeyspace() throws Exception {
    // Make the role a superuser.
    cs.execute(String.format("ALTER ROLE %s with SUPERUSER = true", username));

    createKeyspaceAndVerify(cs2.getSession(), keyspace + "_2");
  }

  @Test
  public void testDeleteKeyspaceWithNoPermissions() throws Exception {
    thrown.expect(UnauthorizedException.class);
    cs2.execute("DROP KEYSPACE " + keyspace);
  }

  @Test
  public void testDeleteKeyspaceWithWrongPermissions() throws Exception {
    grantAllPermissionsExcept(Arrays.asList(DROP, DESCRIBE, AUTHORIZE), KEYSPACE, keyspace,
        username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute("DROP KEYSPACE " + keyspace);
  }

  @Test
  public void testDeleteKeyspaceWithDropPermissionOnDifferentKeyspace() throws Exception {
    createKeyspaceAndVerify(cs.getSession(), anotherKeyspace);

    grantPermission(DROP, KEYSPACE, anotherKeyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute("DROP KEYSPACE " + keyspace);
  }

  @Test
  public void testDeleteKeyspaceWithDropPermission() throws Exception {
    // Grant DROP permission on this test's keyspace.
    grantPermission(DROP, KEYSPACE, keyspace, username);

    deleteKeyspaceAndVerify(cs2.getSession(), keyspace);
  }

  @Test
  public void testDeleteKeyspaceWithDropPermissionOnAllKeyspaces() throws Exception {
    // Grant DROP permission on all keyspaces.
    grantPermissionOnAllKeyspaces(DROP, username);

    deleteKeyspaceAndVerify(cs2.getSession(), keyspace);
  }

  @Test
  public void testSuperuserCanDeleteKeyspace() throws Exception {
    // Make the role a superuser.
    cs.execute(String.format("ALTER ROLE %s with SUPERUSER = true", username));

    deleteKeyspaceAndVerify(cs2.getSession(), keyspace);
  }

  @Test
  public void testCreateTableWithoutPermissions() throws Exception {
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("CREATE table %s.%s (h int, primary key(h))", keyspace, table));
  }

  @Test
  public void testCreateTableWithWrongPermissions() throws Exception {
    // Grant all the permissions except CREATE.
    grantAllPermissionsExcept(Arrays.asList(CREATE, DESCRIBE, AUTHORIZE), KEYSPACE, keyspace,
        username);

    // username shouldn't be able to create a table.
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("CREATE table %s.%s (h int, primary key(h))", keyspace, table));
  }

  @Test
  public void testCreateTableWithCreatePermission() throws Exception {
    // Grant CREATE permission on the keyspace.
    grantPermission(CREATE, KEYSPACE, keyspace, username);

    createTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSuperuserCanCreateTable() throws Exception {
    // Make the role a superuser.
    cs.execute(String.format("ALTER ROLE %s with SUPERUSER = true", username));

    createTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testDeleteTableWithNoPermissions() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("DROP TABLE %s.%s", keyspace, table));
  }

  @Test
  public void testDeleteTableWithWrongPermissions() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(CREATE, DROP, DESCRIBE, AUTHORIZE), TABLE,
        keyspace + "." + table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("DROP TABLE %s.%s", keyspace, table));
  }

  @Test
  public void testDeleteTableWithDropPermissionOnDifferentKeyspace() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    createKeyspaceAndVerify(cs.getSession(), anotherKeyspace);

    grantPermission(DROP, KEYSPACE, anotherKeyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("DROP TABLE %s.%s", keyspace, table));
  }

  @Test
  public void testDeleteTableWithDropPermissionOnDifferentTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    createTableAndVerify(cs.getSession(), keyspace, anotherTable);
    grantPermission(DROP, TABLE, keyspace + "." + anotherTable, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("DROP TABLE %s.%s", keyspace, table));
  }

  @Test
  public void testDeleteTableWithDropPermissionOnKeyspace() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    // Grant DROP permission on this test's keyspace.
    grantPermission(DROP, KEYSPACE, keyspace, username);

    deleteTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testDeleteTableWithDropPermissionOnTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    // Grant DROP permission on this test's keyspace.
    grantPermission(DROP, TABLE, table, username);

    deleteTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testDeleteTableWithDropPermissionOnAllKeyspaces() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    // Grant DROP permission on all keyspaces.
    grantPermissionOnAllKeyspaces(DROP, username);

    deleteTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSuperuserCanDeleteTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    // Make the role a superuser.
    cs.execute(String.format("ALTER ROLE %s with SUPERUSER = true", username));

    deleteTableAndVerify(cs2.getSession(), keyspace, table);
  }

  private void testStatementWithNoPermissions() throws Exception {

  }

  /*
   * SELECT statements tests.
   */

  @Test
  public void testSelectStatementWithNoPermissions() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("SELECT * from %s.%s", keyspace, table));
  }

  @Test
  public void testSelectStatementWithWrongPermissionsOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(SELECT, CREATE, DESCRIBE), TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("SELECT * from %s.%s", keyspace, table));
  }

  @Test
  public void testSelectStatementWithWrongPermissionsOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(SELECT, DESCRIBE), KEYSPACE, keyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("SELECT * from %s.%s", keyspace, table));
  }

  @Test
  public void testSelectStatementWithSelectPermissionOnDifferentTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    String table2 = table + "_2";
    createTableAndInsertRecord(cs.getSession(), keyspace, table2);

    grantPermission(SELECT, TABLE, table2, username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("SELECT * from %s.%s", keyspace, table));
  }

  @Test
  public void testSelectStatementWithSelectPermissionOnDifferentKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    String keyspace2 = keyspace + "_2";

    cs.execute("CREATE KEYSPACE " + keyspace2);
    grantPermission(SELECT, KEYSPACE, keyspace2, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("SELECT * from %s.%s", keyspace, table));
  }

  @Test
  public void testSelectStatementWithSelectPermissionOnTableToDifferentRole() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(SELECT, TABLE, table, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("SELECT * from %s.%s", keyspace, table));
  }

  @Test
  public void testSelectStatementWithSelectPermissionOnKeyspaceToDifferentRole() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(SELECT, KEYSPACE, keyspace, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("SELECT * from %s.%s", keyspace, table));
  }

  @Test
  public void testSelectStatementWithSelectPermissionOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantPermission(SELECT, TABLE, table, username);

    selectAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSelectStatementWithSelectPermissionOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantPermission(SELECT, KEYSPACE, keyspace, username);

    selectAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSelectStatementWithAllPermissionsOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantAllPermission(TABLE, table, username);
    selectAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSelectStatementWithAllPermissionsOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantAllPermission(KEYSPACE, keyspace, username);
    selectAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSuperuserCanSelectFromTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    // Make the role a superuser.
    cs.execute(String.format("ALTER ROLE %s with SUPERUSER = true", username));

    selectAndVerify(cs2.getSession(), keyspace, table);
  }

  /*
   * INSERT statements tests.
   */

  @Test
  public void testInsertStatementWithNoPermissions() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("INSERT INTO %s.%s (h) VALUES (%d)", keyspace, table, VALUE));
  }

  @Test
  public void testInsertStatementWithWrongPermissionsOnTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(MODIFY, CREATE, DESCRIBE), TABLE, table, username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("INSERT INTO %s.%s (h) VALUES (%d)", keyspace, table, VALUE));
  }

  @Test
  public void testInsertStatementWithWrongPermissionsOnKeyspace() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(MODIFY, DESCRIBE), KEYSPACE, keyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("INSERT INTO %s.%s (h) VALUES (%d)", keyspace, table, VALUE));
  }

  @Test
  public void testInsertStatementWithModifyPermissionOnDifferentTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    String table2 = table + "_2";
    createTableAndVerify(cs.getSession(), keyspace, table2);

    grantPermission(MODIFY, TABLE, table2, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("INSERT INTO %s.%s (h) VALUES (%d)", keyspace, table, VALUE));
  }

  @Test
  public void testInsertStatementWithModifyPermissionOnDifferentKeyspace() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    String keyspace2 = keyspace + "_2";
    cs.execute("CREATE KEYSPACE " + keyspace2);
    grantPermission(MODIFY, KEYSPACE, keyspace2, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("INSERT INTO %s.%s (h) VALUES (%d)", keyspace, table, VALUE));
  }

  @Test
  public void testInsertStatementWithModifyPermissionOnTableToDifferentRole() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(MODIFY, TABLE, table, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("INSERT INTO %s.%s (h) VALUES (%d)", keyspace, table, VALUE));
  }

  @Test
  public void testInsertStatementWithModifyPermissionOnKeyspaceToDifferentRole() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(MODIFY, KEYSPACE, keyspace, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("INSERT INTO %s.%s (h) VALUES (%d)", keyspace, table, VALUE));
  }

  @Test
  public void testInsertStatementWithModifyPermissionOnTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(MODIFY, TABLE, table, username);
    insertRow(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testInsertStatementWithModifyPermissionOnKeyspace() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(MODIFY, KEYSPACE, keyspace, username);
    insertRow(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testInsertStatementWithAllPermissionsOnTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantAllPermission(TABLE, table, username);
    insertRow(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testInsertStatementWithAllPermissionsOnKeyspace() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantAllPermission(KEYSPACE, keyspace, username);
    insertRow(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSuperuserCanInsertIntoTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    // Make the role a superuser.
    cs.execute(String.format("ALTER ROLE %s with SUPERUSER = true", username));

    insertRow(cs2.getSession(), keyspace, table);
  }

  /*
   * UPDDATE statements tests.
   */

  @Test
  public void testUpdateStatementWithNoPermissions() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("UPDATE %s.%s SET h = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));
  }

  @Test
  public void testUpdateStatementWithWrongPermissionsOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(MODIFY, CREATE, DESCRIBE), TABLE, table, username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("UPDATE %s.%s SET h = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));
  }

  @Test
  public void testUpdateStatementWithWrongPermissionsOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(DESCRIBE, MODIFY), KEYSPACE, keyspace, username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("UPDATE %s.%s SET h = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));
  }

  @Test
  public void testUpdateStatementWithModifyPermissionOnDifferentTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createTableAndVerify(cs.getSession(), keyspace, anotherTable);
    grantPermission(MODIFY, TABLE, anotherTable, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("UPDATE %s.%s SET h = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));
  }

  @Test
  public void testUpdateStatementWithModifyPermissionOnDifferentKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createKeyspaceAndVerify(cs.getSession(), anotherKeyspace);
    grantPermission(MODIFY, KEYSPACE, anotherKeyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("UPDATE %s.%s SET h = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));
  }

  @Test
  public void testUpdateStatementWithModifyPermissionOnTableToDifferentRole() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(MODIFY, TABLE, table, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("UPDATE %s.%s SET h = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));
  }

  @Test
  public void testUpdateStatementWithModifyPermissionOnKeyspaceToDifferentRole() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(MODIFY, KEYSPACE, keyspace, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("UPDATE %s.%s SET h = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));
  }

  @Test
  public void testUpdateStatementWithModifyPermissionOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantPermission(MODIFY, TABLE, table, username);
    updateRowAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testUpdateStatementWithModifyPermissionOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantPermission(MODIFY, KEYSPACE, keyspace, username);
    updateRowAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testUpdateStatementWithAllPermissionsOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermission(TABLE, table, username);
    updateRowAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testUpdateStatementWithAllPermissionsOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermission(KEYSPACE, keyspace, username);
    updateRowAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSuperuserCanUpdateTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    cs.execute(String.format("ALTER ROLE %s with SUPERUSER = true", username));

    updateRowAndVerify(cs2.getSession(), keyspace, table);
  }

   /*
   * TRUNCATE statements tests.
   */

  @Test
  public void testTruncateStatementWithNoPermissions() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithWrongPermissionsOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(MODIFY, CREATE, DESCRIBE), TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithWrongPermissionsOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(DESCRIBE, MODIFY), KEYSPACE, keyspace, username);

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

  @Test
  public void testTruncateStatementWithModifyPermissionOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantPermission(MODIFY, TABLE, table, username);
    truncateTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testTruncateStatementWithModifyPermissionOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantPermission(MODIFY, KEYSPACE, keyspace, username);
    truncateTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testTruncateStatementWithAllPermissionsOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantAllPermission(TABLE, table, username);
    truncateTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testTruncateStatementWithAllPermissionsOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantAllPermission(KEYSPACE, keyspace, username);
    truncateTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testSuperuserCanTruncateTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    cs.execute(String.format("ALTER ROLE %s with SUPERUSER = true", username));
    truncateTableAndVerify(cs2.getSession(), keyspace, table);
  }

  /*
   * Grant or Revoke test helper methods.
   */

  private void testGrantRevokeRoleWithoutPermissions(GrantRevoke stmtType) throws Exception {
    String r = String.format("%s_role_no_permissions", stmtType);
    createRole(cs.getSession(), r, password, false, false, false);

    thrown.expect(UnauthorizedException.class);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(String.format("GRANT %s TO %s", r, anotherUsername));
    } else {
      cs2.execute(String.format("REVOKE %s FROM %s", r, anotherUsername));
    }
  }

  private void testGrantRevokeRoleWithoutPermissionOnRecipientRole(GrantRevoke stmtType)
      throws Exception {
    String grantedRole = String.format("%s_role_without_permissions_on_recipient", stmtType);
    createRole(cs.getSession(), grantedRole, password, false, false, false);

    // Grant AUTHORIZE on grantedRole.
    grantPermission(AUTHORIZE, ROLE, grantedRole, username);

    cs2.execute(String.format("GRANT %s TO %s", grantedRole, anotherUsername));
    if (stmtType.equals(GrantRevoke.REVOKE)) {
      cs2.execute(String.format("REVOKE %s FROM %s", grantedRole, anotherUsername));
    }
  }

  private void testGrantRevokeRoleWithoutPermissionOnGrantedRole(GrantRevoke stmtType)
      throws Exception {
    String recipientRole = String.format("%s_without_permissions_on_granted", stmtType);
    createRole(cs.getSession(), recipientRole, password, false, false, false);

    // Grant AUTHORIZE on recipientRole.
    grantPermission(AUTHORIZE, ROLE, recipientRole, username);

    thrown.expect(UnauthorizedException.class);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(String.format("GRANT %s TO %s", anotherUsername, recipientRole));
    } else {
      cs2.execute(String.format("REVOKE %s FROM %s", anotherUsername, recipientRole));
    }
  }

  private void testGrantRevokeRoleWithWrongPermissionsOnGrantedAndRecipientRoles(
      GrantRevoke stmtType) throws Exception {
    String recipientRole = String.format("%s_recipient_role_wrong_permissions", stmtType);
    String grantedRole = String.format("%s_granted_role_wrong_permissions", stmtType);
    createRole(cs.getSession(), recipientRole, password, false, false, false);
    createRole(cs.getSession(), grantedRole, password, false, false, false);

    grantAllPermissionsExcept(Arrays.asList(AUTHORIZE, CREATE, DESCRIBE, MODIFY, SELECT),
        ROLE, grantedRole, username);
    grantAllPermissionsExcept(Arrays.asList(AUTHORIZE, CREATE, DESCRIBE, MODIFY, SELECT),
        ROLE, recipientRole, username);

    thrown.expect(UnauthorizedException.class);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(String.format("GRANT %s TO %s", grantedRole, recipientRole));
    } else {
      cs2.execute(String.format("revoke %s FROM %s", grantedRole, recipientRole));
    }
  }

  private void testGrantRevokeRoleWithWrongPermissionsOnAllRoles(GrantRevoke stmtType)
      throws Exception {
    String recipientRole = String.format("%s_recipient_role_wrong_permissions_on_roles", stmtType);
    String grantedRole = String.format("%s_granted_role_wrong_permissions_on_roles", stmtType);
    createRole(cs.getSession(), recipientRole, password, false, false, false);
    createRole(cs.getSession(), grantedRole, password, false, false, false);

    grantAllPermissionsExcept(Arrays.asList(AUTHORIZE, MODIFY, SELECT), ALL_ROLES, "", username);

    thrown.expect(UnauthorizedException.class);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(String.format("GRANT %s TO %s", grantedRole, recipientRole));
    } else {
      cs2.execute(String.format("REVOKE %s FROM %s", grantedRole, recipientRole));
    }
  }

  private void testGrantRevokeRoleWithPermissionOnGrantedAndRecipientRoles(GrantRevoke stmtType)
      throws Exception {
    String recipientRole = String.format("%s_recipient_role_full_permissions", stmtType);
    String grantedRole = String.format("%s_granted_role_full_permissions", stmtType);
    createRole(cs.getSession(), recipientRole, password, false, false, false);
    createRole(cs.getSession(), grantedRole, password, false, false, false);

    grantPermission(AUTHORIZE, ROLE, grantedRole, username);
    grantPermission(AUTHORIZE, ROLE, recipientRole, username);

    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(String.format("GRANT %s TO %s", grantedRole, recipientRole));
    } else {
      // Grant the role first using cassandra role.
      cs.execute(String.format("GRANT %s TO %s", grantedRole, recipientRole));
      cs2.execute(String.format("REVOKE %s FROM %s", grantedRole, recipientRole));
    }
  }

  private void testGrantRevokeRoleWithPermissionOnAllRoles(GrantRevoke stmtType) throws Exception {
    String recipientRole = String.format("%s_recipient_role_full_permissions_on_roles", stmtType);
    String grantedRole = String.format("%s_granted_role_full_permissions_on_roles", stmtType);
    createRole(cs.getSession(), recipientRole, password, false, false, false);
    createRole(cs.getSession(), grantedRole, password, false, false, false);

    grantPermissionOnAllRoles(AUTHORIZE, username);

    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(String.format("GRANT %s TO %s", grantedRole, recipientRole));
    } else {
      // Grant the role first using cassandra role.
      cs.execute(String.format("GRANT %s TO %s", grantedRole, recipientRole));
      cs2.execute(String.format("REVOKE %s FROM %s", grantedRole, recipientRole));
    }
  }

  //
  // GRANT ROLE statements
  //

  @Test
  public void testGrantRoleWithoutPermissions() throws Exception {
    testGrantRevokeRoleWithoutPermissions(GrantRevoke.GRANT);
  }

  // AUTHORIZE permission only on the granted role.
  @Test
  public void testGrantRoleWithoutPermissionOnRecipientRole() throws Exception {
    testGrantRevokeRoleWithoutPermissionOnRecipientRole(GrantRevoke.GRANT);
  }

  // AUTHORIZE permission only on the recipient role.
  @Test
  public void testGrantRoleWithoutPermissionOnGrantedRole() throws Exception {
    testGrantRevokeRoleWithoutPermissionOnGrantedRole(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantRoleWithWrongPermissionsOnGrantedAndRecipientRoles() throws Exception {
    testGrantRevokeRoleWithWrongPermissionsOnGrantedAndRecipientRoles(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantRoleWithWrongPermissionsOnAllRoles() throws Exception {
    testGrantRevokeRoleWithWrongPermissionsOnAllRoles(GrantRevoke.GRANT);
  }

  // AUTHORIZE permission only on the recipient and granted roles.
  @Test
  public void testGrantRoleWithPermissionOnGrantedAndRecipientRoles() throws Exception {
    testGrantRevokeRoleWithPermissionOnGrantedAndRecipientRoles(GrantRevoke.GRANT);
  }

  // AUTHORIZE permission only on ALL ROLES.
  @Test
  public void testGrantRoleWithPermissionOnALLRoles() throws Exception {
    testGrantRevokeRoleWithPermissionOnAllRoles(GrantRevoke.GRANT);
  }

  //
  // REVOKE ROLE statements
  //

  @Test
  public void testRevokeRoleWithoutPermissions() throws Exception {
    testGrantRevokeRoleWithoutPermissions(GrantRevoke.REVOKE);
  }

  // AUTHORIZE permission only on the granted role.
  @Test
  public void testRevokeRoleWithoutPermissionOnRecipientRole() throws Exception {
    testGrantRevokeRoleWithoutPermissionOnRecipientRole(GrantRevoke.REVOKE);
  }

  // AUTHORIZE permission only on the recipient role.
  @Test
  public void testRevokeRoleWithoutPermissionOnRevokeedRole() throws Exception {
    testGrantRevokeRoleWithoutPermissionOnGrantedRole(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokeRoleWithWrongPermissionsOnGrantedAndRecipientRoles() throws Exception {
    testGrantRevokeRoleWithWrongPermissionsOnGrantedAndRecipientRoles(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokeRoleWithWrongPermissionsOnAllRoles() throws Exception {
    testGrantRevokeRoleWithWrongPermissionsOnAllRoles(GrantRevoke.REVOKE);
  }

  // AUTHORIZE permission only on the recipient and granted roles.
  @Test
  public void testRevokeRoleWithPermissionOnGrantedAndRecipientRoles() throws Exception {
    testGrantRevokeRoleWithPermissionOnGrantedAndRecipientRoles(GrantRevoke.REVOKE);
  }

  // AUTHORIZE permission only on ALL ROLES.
  @Test
  public void testRevokeRoleWithPermissionOnALLRoles() throws Exception {
    testGrantRevokeRoleWithPermissionOnAllRoles(GrantRevoke.REVOKE);
  }

  //
  // Grant/Revoke permissions on keyspaces/tables helper methods.
  //
  private String getGrantOnKeyspaceStmt() {
    return String.format("GRANT CREATE ON KEYSPACE %s TO %s", keyspace, anotherUsername);
  }

  private String getRevokeFromKeyspaceStmt() {
    return String.format("REVOKE CREATE ON KEYSPACE %s FROM %s", keyspace, anotherUsername);
  }

  private void grantAuthorizePermissionOnKeyspace() throws Exception {
    cs.execute(getGrantOnKeyspaceStmt());
  }

  private void testGrantAuthorizePermissionOnKeyspaceFails() throws Exception {
    thrown.expect(UnauthorizedException.class);
    cs2.execute(getGrantOnKeyspaceStmt());
  }

  private void testRevokeAuthorizePermissionFromKeyspaceFails() throws Exception {
    // First grant the permission using cassandra role.
    grantAuthorizePermissionOnKeyspace();
    thrown.expect(UnauthorizedException.class);
    cs2.execute(getRevokeFromKeyspaceStmt());
  }

  private void testGrantRevokePermissionOnKeyspaceWithNoPermissions(GrantRevoke stmtType)
      throws Exception {
    if (stmtType.equals(GrantRevoke.GRANT)) {
      testGrantAuthorizePermissionOnKeyspaceFails();
    } else {
      testRevokeAuthorizePermissionFromKeyspaceFails();
    }
  }

  private void testGrantRevokePermissionOnKeyspaceWithWrongPermissionsOnKeyspace(
      GrantRevoke stmtType) throws Exception {
    grantAllPermissionsExcept(Arrays.asList(AUTHORIZE, DESCRIBE), KEYSPACE, keyspace, username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      testGrantAuthorizePermissionOnKeyspaceFails();
    } else {
      testRevokeAuthorizePermissionFromKeyspaceFails();
    }
  }

  private void testGrantRevokePermissionOnKeyspaceWithWrongPermissionsOnAllKeyspaces(
      GrantRevoke stmtType) throws Exception {
    grantAllPermissionsExcept(Arrays.asList(AUTHORIZE, DESCRIBE), ALL_KEYSPACES, "", username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      testGrantAuthorizePermissionOnKeyspaceFails();
    } else {
      testRevokeAuthorizePermissionFromKeyspaceFails();
    }
  }

  private void testGrantRevokePermissionOnKeyspaceWithAuthorizePermissionOnKeyspace(
      GrantRevoke stmtType) throws Exception {
    grantPermission(AUTHORIZE, KEYSPACE, keyspace, username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(getGrantOnKeyspaceStmt());
    } else {
      cs.execute(getGrantOnKeyspaceStmt());
      cs2.execute(getRevokeFromKeyspaceStmt());
    }
  }

  private void testGrantRevokePermissionOnKeyspaceWithAuthorizePermissionOnAllKeyspaces(
      GrantRevoke stmtType) throws Exception {
    grantPermissionOnAllKeyspaces(AUTHORIZE, username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(getGrantOnKeyspaceStmt());
    } else {
      cs.execute(getGrantOnKeyspaceStmt());
      cs2.execute(getRevokeFromKeyspaceStmt());
    }
  }

  private String getGrantOnTableStmt() {
    return String.format("GRANT SELECT ON TABLE %s.%s TO %s", keyspace, table, anotherUsername);
  }

  private String getRevokeFromTableStmt() {
    return String.format("REVOKE SELECT ON TABLE %s.%s FROM %s", keyspace, table,
        anotherUsername);
  }

  private void testGrantPermissionOnTableFails() throws Exception {
    thrown.expect(UnauthorizedException.class);
    cs2.execute(getGrantOnTableStmt());
  }

  private void testRevokePermissionOnTableFails() throws Exception {
    // First grant the permission using cassandra role.
    cs.execute(getGrantOnTableStmt());
    thrown.expect(UnauthorizedException.class);
    cs2.execute(getRevokeFromTableStmt());
  }

  private void testGrantRevokePermissionOnTableWithNoPermissions(GrantRevoke stmtType)
      throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      testGrantPermissionOnTableFails();
    } else {
      testRevokePermissionOnTableFails();
    }
  }

  private void testGrantRevokePermissionOnTableWithWrongPermissionsOnTable(GrantRevoke stmtType)
      throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantAllPermissionsExcept(Arrays.asList(AUTHORIZE, CREATE, DESCRIBE), TABLE, table, username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      testGrantPermissionOnTableFails();
    } else {
      testRevokePermissionOnTableFails();
    }
  }

  private void testGrantRevokePermissionOnTableWithWrongPermissionsOnKeyspace(GrantRevoke stmtType)
      throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantAllPermissionsExcept(Arrays.asList(AUTHORIZE, DESCRIBE), KEYSPACE, keyspace, username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      testGrantPermissionOnTableFails();
    } else {
      testRevokePermissionOnTableFails();
    }
  }

  private void testGrantRevokePermissionOnTableWithWrongPermissionsOnAllKeyspaces(
      GrantRevoke stmtType) throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantAllPermissionsExcept(Arrays.asList(AUTHORIZE, DESCRIBE), ALL_KEYSPACES, "", username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      testGrantPermissionOnTableFails();
    } else {
      testRevokePermissionOnTableFails();
    }
  }

  private void testGrantRevokePermissionOnTableWithAuthorizePermissionOnTable(GrantRevoke stmtType)
      throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(AUTHORIZE, TABLE, table, username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(getGrantOnTableStmt());
    } else {
      // First grant the permission using cassandra role.
      cs.execute(getGrantOnTableStmt());
      cs2.execute(getRevokeFromTableStmt());
    }
  }

  private void testGrantRevokePermissionOnTableWithAuthorizePermissionOnKeyspace(
      GrantRevoke stmtType) throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(AUTHORIZE, KEYSPACE, keyspace, username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(getGrantOnTableStmt());
    } else {
      // First grant the permission using cassandra role.
      cs.execute(getGrantOnTableStmt());
      cs2.execute(getRevokeFromTableStmt());
    }
  }

  private void testGrantRevokePermissionOnTableWithAuthorizePermissionOnAllKeyspaces(
      GrantRevoke stmtType) throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(AUTHORIZE, ALL_KEYSPACES, "", username);
    if (stmtType.equals(GrantRevoke.GRANT)) {
      cs2.execute(getGrantOnTableStmt());
    } else {
      // First grant the permission using cassandra role.
      cs.execute(getGrantOnTableStmt());
      cs2.execute(getRevokeFromTableStmt());
    }
  }

  //
  // GRANT PERMISSION statements.
  //

  @Test
  public void testGrantPermissionOnKeyspaceWithNoPermissions() throws Exception {
    testGrantRevokePermissionOnKeyspaceWithNoPermissions(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnKeyspaceWithWrongPermissionsOnKeyspace() throws Exception {
    testGrantRevokePermissionOnKeyspaceWithWrongPermissionsOnKeyspace(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnKeyspaceWithWrongPermissionsOnAllKeyspaces() throws Exception {
    testGrantRevokePermissionOnKeyspaceWithWrongPermissionsOnAllKeyspaces(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnKeyspaceWithAuthorizePermissionOnKeyspace() throws Exception {
    testGrantRevokePermissionOnKeyspaceWithAuthorizePermissionOnKeyspace(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnKeyspaceWithAuthorizePermissionOnAllKeyspaces()
      throws Exception {
    testGrantRevokePermissionOnKeyspaceWithAuthorizePermissionOnAllKeyspaces(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnTableWithNoPermissions() throws Exception {
    testGrantRevokePermissionOnTableWithNoPermissions(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnTableWithWrongPermissionsOnTable() throws Exception {
    testGrantRevokePermissionOnTableWithWrongPermissionsOnTable(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnTableWithWrongPermissionsOnKeyspace() throws Exception {
    testGrantRevokePermissionOnTableWithWrongPermissionsOnKeyspace(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnTableWithWrongPermissionsOnAllKeyspaces() throws Exception {
    testGrantRevokePermissionOnTableWithWrongPermissionsOnAllKeyspaces(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnTableWithAuthorizePermissionOnTable() throws Exception {
    testGrantRevokePermissionOnTableWithAuthorizePermissionOnTable(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnTableWithAuthorizePermissionOnKeyspace() throws Exception {
    testGrantRevokePermissionOnTableWithAuthorizePermissionOnKeyspace(GrantRevoke.GRANT);
  }

  @Test
  public void testGrantPermissionOnTableWithAuthorizePermissionOnAllKeyspaces() throws Exception {
    testGrantRevokePermissionOnTableWithAuthorizePermissionOnAllKeyspaces(GrantRevoke.GRANT);
  }

  //
  // REVOKE PERMISSION statements.
  //

  @Test
  public void testRevokePermissionOnKeyspaceWithNoPermissions() throws Exception {
    testGrantRevokePermissionOnKeyspaceWithNoPermissions(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnKeyspaceWithWrongPermissionsOnKeyspace() throws Exception {
    testGrantRevokePermissionOnKeyspaceWithWrongPermissionsOnKeyspace(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnKeyspaceWithWrongPermissionsOnAllKeyspaces() throws Exception {
    testGrantRevokePermissionOnKeyspaceWithWrongPermissionsOnAllKeyspaces(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnKeyspaceWithAuthorizePermissionOnKeyspace() throws Exception {
    testGrantRevokePermissionOnKeyspaceWithAuthorizePermissionOnKeyspace(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnKeyspaceWithAuthorizePermissionOnAllKeyspaces()
      throws Exception {
    testGrantRevokePermissionOnKeyspaceWithAuthorizePermissionOnAllKeyspaces(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnTableWithNoPermissions() throws Exception {
    testGrantRevokePermissionOnTableWithNoPermissions(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnTableWithWrongPermissionsOnTable() throws Exception {
    testGrantRevokePermissionOnTableWithWrongPermissionsOnTable(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnTableWithWrongPermissionsOnKeyspace() throws Exception {
    testGrantRevokePermissionOnTableWithWrongPermissionsOnKeyspace(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnTableWithWrongPermissionsOnAllKeyspaces() throws Exception {
    testGrantRevokePermissionOnTableWithWrongPermissionsOnAllKeyspaces(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnTableWithAuthorizePermissionOnTable() throws Exception {
    testGrantRevokePermissionOnTableWithAuthorizePermissionOnTable(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnTableWithAuthorizePermissionOnKeyspace() throws Exception {
    testGrantRevokePermissionOnTableWithAuthorizePermissionOnKeyspace(GrantRevoke.REVOKE);
  }

  @Test
  public void testRevokePermissionOnTableWithAuthorizePermissionOnAllKeyspaces() throws Exception {
    testGrantRevokePermissionOnTableWithAuthorizePermissionOnAllKeyspaces(GrantRevoke.REVOKE);
  }

  @Test
  public void testPreparedCreateKeyspaceWithCreatePermission() throws Exception {
    grantPermissionOnAllKeyspaces(CREATE, username);

    // Prepare and execute statement.
    String createKeyspaceStmt = "CREATE KEYSPACE prepared_keyspace";
    PreparedStatement stmt = cs2.prepare(createKeyspaceStmt);
    cs2.execute(stmt.bind());

    revokePermission(CREATE, ALL_KEYSPACES, "", username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedCreateTableWithCreatePermission() throws Exception {
    grantPermission(CREATE, KEYSPACE, keyspace, username);

    cs2.execute("USE " + keyspace);
    // Prepare and execute statement.
    String createTableStmt = String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h))",
        keyspace, "prepared_table");
    PreparedStatement stmt = cs2.prepare(createTableStmt);
    cs2.execute(stmt.bind());

    revokePermission(CREATE, KEYSPACE, keyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedAlterTableWithAlterPermission() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(ALTER, TABLE, table, username);

    // Prepare and execute statement.
    String alterTableStmt = String.format("ALTER TABLE %s.%s ADD v2 int", keyspace, table);
    PreparedStatement stmt = cs2.prepare(alterTableStmt);
    cs2.execute(stmt.bind());

    revokePermission(ALTER, TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testTruncateTableWithModifyPermission() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(MODIFY, TABLE, table, username);

    // Prepare and excecute statement.
    String truncateStmt = String.format("TRUNCATE %s.%s", keyspace, table);
    PreparedStatement stmt = cs2.prepare(truncateStmt);
    cs2.execute(stmt.bind());

    revokePermission(MODIFY, TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedInsertStmtWithSuperuserRole() throws Exception {
    createRole(cs.getSession(), anotherUsername, password, true, true, false);
    try (ClusterAndSession cs3 = connectWithCredentials(anotherUsername, password)) {
      createTableAndVerify(cs.getSession(), keyspace, table);

      // Prepare and execute statement.
      String insertStmt = String.format("INSERT INTO %s.%s (h, v) VALUES (?, ?)", keyspace, table);
      PreparedStatement stmt = cs3.prepare(insertStmt);

      ResultSet rs = cs3.execute(stmt.bind(3, 5));

      cs.execute(String.format("ALTER ROLE %s with SUPERUSER = false", anotherUsername));
      Thread.sleep(TIME_SLEEP_MS);

      thrown.expect(UnauthorizedException.class);
      rs = cs3.execute(stmt.bind(4, 2));
    }
  }

  @Test
  public void testPreparedInsertStmtWithModifyPermission() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    grantPermission(MODIFY, TABLE, table, username);

    // Prepare and execute statement.
    String insertStmt = String.format("INSERT INTO %s.%s (h, v) VALUES (?, ?)", keyspace, table);
    PreparedStatement stmt = cs2.prepare(insertStmt);

    ResultSet rs = cs2.execute(stmt.bind(3, 5));

    // Revoke the MODIFY permissions so the next execution of the prepared statement fails.
    revokePermission(MODIFY, TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    rs = cs2.execute(stmt.bind(4, 2));
  }

  @Test
  public void testPreparedSelectStmtWithSelectPermission() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantPermission(SELECT, TABLE, table, username);

    // Prepare and execute statement.
    String selectStmt = String.format("SELECT * FROM %s.%s", keyspace, table);
    PreparedStatement stmt = cs2.prepare(selectStmt);

    ResultSet rs = cs2.execute(stmt.bind());
    List<Row> rows = rs.all();
    assertEquals(1, rows.size());
    assertEquals(VALUE, rows.get(0).getInt("h"));
    assertEquals(VALUE, rows.get(0).getInt("v"));

    revokePermission(SELECT, TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    rs = cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedUpdateStmtWithModifyPermission() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantPermission(MODIFY, TABLE, table, username);

    // Prepare and execute statement.
    String updateStmt = String.format("UPDATE %s.%s set v = 1 WHERE h = ?", keyspace, table);
    PreparedStatement stmt = cs2.prepare(updateStmt);

    cs2.execute(stmt.bind(VALUE));

    revokePermission(MODIFY, TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind(VALUE));
  }

  @Test
  public void testPreparedDeleteStmtWithModifyPermission() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantPermission(MODIFY, TABLE, table, username);

    // Prepare and execute statement.
    String deleteStmt = String.format("DELETE FROM %s.%s WHERE h = ?", keyspace, table);
    PreparedStatement stmt = cs2.prepare(deleteStmt);
    cs2.execute(stmt.bind(VALUE));

    revokePermission(MODIFY, TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind(VALUE));
  }

  private void testPreparedGrantRevokeRoleStatementWithAuthorizePermission(GrantRevoke stmtType)
      throws Exception {
    String recipientRole = String.format("%s_recipient_%s", username, stmtType);
    String grantedRole = String.format("%s_granted_%s", username, stmtType);
    createRole(cs.getSession(), recipientRole, password, false, false, false);
    createRole(cs.getSession(), grantedRole, password, false, false, false);

    grantPermission(AUTHORIZE, ROLE, grantedRole, username);
    grantPermission(AUTHORIZE, ROLE, recipientRole, username);

    String stmt;
    if (stmtType.equals(GrantRevoke.GRANT)) {
      stmt = String.format("GRANT %s TO %s", grantedRole, recipientRole);
    } else {
      // Grant the role first using cassandra role.
      cs.execute(String.format("GRANT %s TO %s", grantedRole, recipientRole));

      stmt = String.format("REVOKE %s FROM %s", grantedRole, recipientRole);
    }
    PreparedStatement preparedStatement = cs2.prepare(stmt);
    revokePermission(AUTHORIZE, ROLE, grantedRole, username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(preparedStatement.bind());
  }

  private void testPreparedGrantRevokeRoleStatementWithSuperuserRole(GrantRevoke stmtType)
      throws Exception {
    String recipientRole = String.format("recipient_%s_%s_test", username, stmtType);
    String grantedRole = String.format("granted_%s_%s_test", username, stmtType);
    createRole(cs.getSession(), recipientRole, password, false, false, false);
    createRole(cs.getSession(), grantedRole, password, false, false, false);

    createRole(cs.getSession(), anotherUsername, password, true, true, false);
    try (ClusterAndSession cs3 = connectWithCredentials(anotherUsername, password)) {
      String stmt;
      if (stmtType.equals(GrantRevoke.GRANT)) {
        stmt = String.format("GRANT %s TO %s", grantedRole, recipientRole);
      } else {
        // Grant the role first using cassandra role.
        cs.execute(String.format("GRANT %s TO %s", grantedRole, recipientRole));

        stmt = String.format("REVOKE %s FROM %s", grantedRole, recipientRole);
      }
      PreparedStatement preparedStatement = cs3.prepare(stmt);

      cs.execute(String.format("ALTER ROLE %s with SUPERUSER = false", anotherUsername));
      Thread.sleep(TIME_SLEEP_MS);

      thrown.expect(UnauthorizedException.class);
      cs3.execute(preparedStatement.bind());
    }
  }

  @Test
  public void testPreparedGrantRoleStatementWithAuthorizePermission() throws Exception {
    testPreparedGrantRevokeRoleStatementWithAuthorizePermission(GrantRevoke.GRANT);
  }

  @Test
  public void testPreparedRevokeRoleStatementWithAuthorizePermission() throws Exception {
    testPreparedGrantRevokeRoleStatementWithAuthorizePermission(GrantRevoke.REVOKE);
  }

  @Test
  public void testPreparedGrantRoleStatementWithSuperuserRole() throws Exception {
    testPreparedGrantRevokeRoleStatementWithSuperuserRole(GrantRevoke.GRANT);
  }

  @Test
  public void testPreparedRevokeRoleStatementWithSuperuserRole() throws Exception {
    testPreparedGrantRevokeRoleStatementWithSuperuserRole(GrantRevoke.REVOKE);
  }

  @Test
  public void testPreparedGrantPermissionOnKeyspaceWithAuthorizePermission() throws Exception {
    grantPermission(AUTHORIZE, KEYSPACE, keyspace, username);

    String grantPermissionStmt = String.format("GRANT CREATE ON KEYSPACE %s to %s",
        keyspace, username);
    PreparedStatement stmt = cs2.prepare(grantPermissionStmt);
    cs2.execute(stmt.bind());

    revokePermission(AUTHORIZE, KEYSPACE, keyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedGrantPermissionOnTableWithAuthorizePermission() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(AUTHORIZE, TABLE, table, username);

    String grantPermissionStmt = String.format("GRANT MODIFY ON TABLE %s.%s to %s",
        keyspace, table, username);
    PreparedStatement stmt = cs2.prepare(grantPermissionStmt);
    cs2.execute(stmt.bind());

    revokePermission(AUTHORIZE, TABLE, table, username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedGrantPermissionOnRoleStmtWithAuthorizePermission() throws Exception {
    createRole(cs.getSession(), anotherUsername, password, false, false, false);

    grantPermission(AUTHORIZE, ROLE, anotherUsername, username);

    String grantPermissionStmt = String.format("GRANT DROP ON ROLE %s to %s",
        anotherUsername, username);
    PreparedStatement stmt = cs2.prepare(grantPermissionStmt);
    cs2.execute(stmt.bind());

    revokePermission(AUTHORIZE, ROLE, anotherUsername, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedGrantPermissionOnAllKeyspaesWithAuthorizePermission() throws Exception {
    grantPermissionOnAllKeyspaces(AUTHORIZE, username);

    String grantPermissionStmt = String.format("GRANT SELECT ON ALL KEYSPACES TO %s", username);
    PreparedStatement stmt = cs2.prepare(grantPermissionStmt);
    cs2.execute(stmt.bind());

    revokePermission(AUTHORIZE, ALL_KEYSPACES, "", username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedGrantPermissionOnAllRolesWithAuthorizePermission() throws Exception {
    grantPermissionOnAllRoles(AUTHORIZE, username);

    String grantPermissionStmt = String.format("GRANT DROP ON ALL ROLES TO %s", username);
    PreparedStatement stmt = cs2.prepare(grantPermissionStmt);
    cs2.execute(stmt.bind());

    revokePermission(AUTHORIZE, ALL_ROLES, "", username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedDropRoleStmtWithDropPermission() throws Exception {
    createRole(cs.getSession(), anotherUsername, password, false, false, false);
    grantPermission(DROP, ROLE, anotherUsername, username);

    String dropStmt = String.format("DROP ROLE %s", anotherUsername);
    PreparedStatement stmt = cs2.prepare(dropStmt);
    cs2.execute(stmt.bind());

    // Create it again.
    createRole(cs.getSession(), anotherUsername, password, false, false, false);
    revokePermission(DROP, ROLE, anotherUsername, username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedDropKeyspaceStmtWithDropPermission() throws Exception {
    String newKeyspace = "prepared_keyspace";
    createKeyspaceAndVerify(cs.getSession(), newKeyspace);

    // Permission has to be granted on ALL KEYSPACES. Granting DROP permission on a specific
    // keyspace only authorizes the user to drop tables in that keyspace, but not to drop the
    // keyspace.
    grantPermissionOnAllKeyspaces(DROP, username);

    String dropStmt = String.format("DROP KEYSPACE %s", newKeyspace);
    PreparedStatement stmt = cs2.prepare(dropStmt);
    cs2.execute(stmt.bind());

    revokePermission(DROP, ALL_KEYSPACES, "", username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testPreparedDropTableStmtWithDropPermission() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    grantPermission(DROP, TABLE, String.format("%s.%s", keyspace, table), username);

    String dropStmt = String.format("DROP TABLE %s.%s", keyspace, table);
    PreparedStatement stmt = cs2.prepare(dropStmt);
    cs2.execute(stmt.bind());

    createTableAndVerify(cs.getSession(), keyspace, table);
    revokePermission(DROP, TABLE, String.format("%s.%s", keyspace, table), username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }

  @Test
  public void testOperationsOnKeyspaceByCreatorRole() throws Exception {
    grantPermissionOnAllKeyspaces(CREATE, username);

    String keyspace2 = keyspace + "_2";

    cs2.execute(String.format("CREATE KEYSPACE %s", keyspace2));

    // Revoke CREATE on ALL KEYSPACES to ensure that we are allowed to create tables on keyspace2
    // because we were granted CREATE permission on the new keyspace.
    revokePermission(CREATE, ALL_KEYSPACES, "", username);

    // Create a new table to test the CREATE permission.
    cs2.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h))",
        keyspace2, table));

    // Create another table using superuser cassandra. Role 'username' shouldn't have any
    // permissions granted on this table because it's not the creator, but because it has all the
    // permissions granted on the keyspace, 'username' should be able to do any operations on the
    // table.
    String table2 = table + "_2";
    cs2.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h))",
        keyspace2, table2));

    // Verify that we can insert a value in table2.
    cs2.execute (String.format("INSERT INTO %s.%s (h, v) VALUES (%d, %d)",
        keyspace2, table2, VALUE, VALUE));

    // Verify that we can read a value.
    ResultSet rs = cs2.execute (String.format("SELECT * from %s.%s", keyspace2, table2));
    assertEquals(1, rs.all().size());

    // Verify that we can update a value.
    cs2.execute(String.format("UPDATE %s.%s SET v = %d WHERE h = %d",
        keyspace2, table2, VALUE + 1, VALUE));

    // Verify that we can delete a value.
    cs2.execute(String.format("DELETE FROM %s.%s WHERE h = %d", keyspace2, table2, VALUE));

    // Verify that we can alter the table.
    cs2.execute(String.format("ALTER TABLE %s.%s ADD v2 int", keyspace2, table2));

    // Verify that we can drop the table.
    cs2.execute(String.format("DROP TABLE %s.%s", keyspace2, table2));

    // Drop the table we created so that we can delete the keyspace (it needs to be empty).
    cs2.execute(String.format("DROP TABLE %s.%s", keyspace2, table));

    // Verify that we can delete the keyspace.
    cs2.execute(String.format("DROP KEYSPACE %s", keyspace2));
  }

  @Test
  public void testOperationsOnTableByCreatorRole() throws Exception {
    grantPermission(CREATE, KEYSPACE, keyspace, username);

    cs2.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h))",
        keyspace, table));

    // Verify that we can insert a value.
    cs2.execute (String.format("INSERT INTO %s.%s (h, v) VALUES (%d, %d)",
        keyspace, table, VALUE, VALUE));

    // Verify that we can read a value.
    ResultSet rs = cs2.execute (String.format("SELECT * from %s.%s", keyspace, table));
    assertEquals(1, rs.all().size());

    // Verify that we can update a value.
    cs2.execute(String.format("UPDATE %s.%s SET v = %d WHERE h = %d",
        keyspace, table, VALUE + 1, VALUE));

    // Verify that we can delete a value.
    cs2.execute(String.format("DELETE FROM %s.%s WHERE h = %d", keyspace, table, VALUE));

    // Verify that we can alter the table.
    cs2.execute(String.format("ALTER TABLE %s.%s ADD v2 int", keyspace, table));

    // Verify that we can drop the table.
    cs2.execute(String.format("DROP TABLE %s.%s", keyspace, table));
  }

  @Test
  public void testOperationsOnRolesByCreatorRole() throws Exception {
    grantPermissionOnAllRoles(CREATE, username);

    String role1 = username + "_1";
    String role2 = username + "_2";

    // Create two roles.
    cs2.execute(String.format("CREATE ROLE %s", role1));
    cs2.execute(String.format("CREATE ROLE %s", role2));

    // Alter role1.
    cs2.execute(String.format("ALTER ROLE %s WITH LOGIN = TRUE", role1));

    // Grant role1 to role2. It should succeed because we should have AUTHORIZE permission on both
    // roles.
    cs2.execute(String.format("GRANT %s to %s", role1, role2));

    // Drop both roles.
    cs2.execute(String.format("DROP ROLE %s", role1));
    cs2.execute(String.format("DROP ROLE %s", role2));
  }

  @Test
  public void testCreateKeyspaceStmtGrantsPermissionsToCreator() throws Exception {
    // Grant CREATE permission on ALL KEYSPACES so that we can create a new keyspace.
    grantPermissionOnAllKeyspaces(CREATE, username);

    // Crete the keyspace.
    String keyspace2 = keyspace + "_2";
    cs2.execute(String.format("CREATE KEYSPACE %s", keyspace2));

    String resource = String.format("data/%s", keyspace2);

    assertPermissionsGranted(cs.getSession(), username, resource, ALL_PERMISSIONS_FOR_KEYSPACE);
  }

  @Test
  public void testCreateTableStmtGrantsPermissionsToCreator() throws Exception {
    // Grant CREATE permission on keyspace so that we can create a new table.
    grantPermission(CREATE, KEYSPACE, keyspace, username);

    // Create the table.
    cs2.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h))",
        keyspace, table));

    List<String> expectedPermissions =
        Arrays.asList("ALTER", "AUTHORIZE", "DROP", "MODIFY", "SELECT");
    String resoure = String.format("data/%s/%s", keyspace, table);

    assertPermissionsGranted(cs.getSession(), username, resoure, expectedPermissions);
  }

  @Test
  public void testCreateRoleStmtGrantsPermissionsToCreator() throws Exception {
    // Grant CREATE permission on ALL ROLES so that we can create a new role.
    grantPermissionOnAllRoles(CREATE, username);

    // Create a new role.
    cs2.execute(String.format("CREATE ROLE %s", anotherUsername));

    List<String> expectedPermissions = Arrays.asList("ALTER", "AUTHORIZE", "DROP");
    String resource = String.format("roles/%s", anotherUsername);

    assertPermissionsGranted(cs.getSession(), username, resource, expectedPermissions);
  }

  @Test
  public void testDeletingKeyspaceRemovesPermissionsToo() throws Exception {
    String keyspace2 = keyspace + "_2";
    cs.execute(String.format("CREATE KEYSPACE %s", keyspace2));

    grantAllPermission(KEYSPACE, keyspace2, username);
    String resource = String.format("data/%s", keyspace2);
    verifySomePermissionsGranted(username, resource);

    cs.execute(String.format("DROP KEYSPACE %s", keyspace2));
    verifyPermissionsDeleted(username, resource);
  }

  @Test
  public void testDeletingTableRemovesPermissionsToo() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    grantAllPermission(TABLE, table, username);
    String resource = String.format("data/%s/%s", keyspace, table);
    verifySomePermissionsGranted(username, resource);

    cs.execute(String.format("DROP TABLE %s.%s", keyspace, table));
    verifyPermissionsDeleted(username, resource);
  }

  @Test
  public void testDeletingRoleRemovesPermissionsToo() throws Exception {
    createRole(cs.getSession(), anotherUsername, password, false, false, false);

    grantAllPermission(ROLE, anotherUsername, username);
    String resource = String.format("roles/%s", anotherUsername);
    verifySomePermissionsGranted(username, resource);

    cs.execute(String.format("DROP ROLE %s", anotherUsername));
    verifyPermissionsDeleted(username, resource);
  }

  @Test
  public void testNewKeyspaceWithOldNameDoesNotGetOldPermissions() throws Exception {
    String keyspace2 = keyspace + "_2";

    cs.execute(String.format("CREATE KEYSPACE %s", keyspace2));

    // Grant all the permissions to username role.
    grantAllPermission(KEYSPACE, keyspace2, username);

    // Create a table and insert a record to verify that username role received the permissions.
    createTableAndInsertRecord(cs2.getSession(), keyspace2, table);

    // Drop the table and keyspace.
    cs2.execute(String.format("DROP TABLE %s.%s", keyspace2, table));
    cs2.execute(String.format("DROP KEYSPACE %s", keyspace2));

    // Create the keyspace again.
    cs.execute(String.format("CREATE KEYSPACE %s", keyspace2));

    // Sleep to give the cache some time to be refreshed.
    Thread.sleep(TIME_SLEEP_MS);

    // Verify that username role can't create a table in the new keyspace.
    thrown.expect(UnauthorizedException.class);
    createTableAndVerify(cs2.getSession(), keyspace2, table);
  }

  @Test
  public void testNewTableWithOldNameDoesNotGetOldPermissions() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    // Grant all the permissions to username role.
    grantAllPermission(TABLE, table, username);

    // username role should be able to insert a row.
    insertRow(cs2.getSession(), keyspace, table);

    cs.execute(String.format("DROP TABLE %s.%s", keyspace, table));

    // Create a new table with the same name.
    createTableAndVerify(cs.getSession(), keyspace, table);

    // Sleep to give the cache some time to be refreshed.
    Thread.sleep(TIME_SLEEP_MS);

    // Verify that we can't insert a row again since we haven't granted any permissions to
    // username.
    thrown.expect(UnauthorizedException.class);
    insertRow(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testNewRoleWithOldNameDoesNotGetOldPermissions() throws Exception {
    String role1 = username + "_1";
    String role2 = username + "_2";
    String role3 = username + "_3";

    // Create the roles.
    createRole(cs.getSession(), role1, password, false, false, false);
    createRole(cs.getSession(), role2, password, false, false, false);
    createRole(cs.getSession(), role3, password, false, false, false);

    // Grant all the permissions to username role on the roles we just created.
    grantAllPermission(ROLE, role1, username);
    grantAllPermission(ROLE, role2, username);
    grantAllPermission(ROLE, role3, username);

    verifySomePermissionsGranted(username, "roles/" + role1);
    verifySomePermissionsGranted(username, "roles/" + role2);
    verifySomePermissionsGranted(username, "roles/" + role3);

    // Verify that username role can grant role1 to role2 (AUTHORIZE permissions on both roles
    // needed to do this).
    cs2.execute(String.format("GRANT %s to %s", role1, role2));

    // Used to verify that username role has permissions on role2 and role3 roles.
    cs2.execute(String.format("GRANT %s to %s", role2, role3));

    // Drop role1 role.
    cs2.execute(String.format("DROP ROLE %s", role1));

    // Create role1 role again.
    createRole(cs.getSession(), role1, password, false, false, false);

    // Sleep to give the cache some time to be refreshed.
    Thread.sleep(TIME_SLEEP_MS);

    // Verify that we can't grant role1 to role3 since username role shouldn't have any permissions
    // on role1 role.
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("GRANT %s to %s", role1, role3));
  }

  // Test that we can grant and revoke permissions on a table without using the keyword TABLE before
  // the table name.
  @Test
  public void testGrantPermissionOnTableWithoutUsingKeywordTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    cs.execute(String.format("GRANT MODIFY ON %s.%s TO %s", keyspace, table, username));
    String canonicalResource = String.format("data/%s/%s", keyspace, table);
    assertPermissionsGranted(cs.getSession(), username, canonicalResource, Arrays.asList(MODIFY));
  }

  @Test
  public void testRevokePermissionOnTableWithoutUsingKeywordTable() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    String canonicalResource = String.format("data/%s/%s", keyspace, table);

    grantPermission(SELECT, TABLE, String.format("%s.%s", keyspace, table), username);
    assertPermissionsGranted(cs.getSession(), username, canonicalResource, Arrays.asList(SELECT));

    cs.execute(String.format("REVOKE SELECT ON %s.%s FROM %s", keyspace, table, username));
    assertPermissionsGranted(cs.getSession(), username, canonicalResource, Arrays.asList());
  }

  // This tests the fix for issue https://github.com/YugaByte/yugabyte-db/issues/592.
  @Test
  public void testAlterStmtFailsWihoutProperties() throws Exception {
    thrown.expect(SyntaxError.class);
    thrown.expectMessage("expecting WITH");
    cs.execute(String.format("ALTER ROLE %s", username));
  }

  @Test
  public void testAlterModifiesProperties() throws Exception {
    createRole(cs.getSession(), anotherUsername, "", false, false, false);
    String newPassword = "p";
    cs.execute(String.format(
        "ALTER ROLE %s WITH LOGIN = true AND SUPERUSER = true AND PASSWORD = '%s'",
        anotherUsername, newPassword));
    ResultSet rs = cs.execute(String.format("SELECT * FROM system_auth.roles WHERE role = '%s'",
        anotherUsername));
    List<Row> list = rs.all();
    assertEquals(1, list.size());
    assert(list.get(0).getBool("can_login"));
    assert(list.get(0).getBool("is_superuser"));
    checkConnectivity(true, anotherUsername, newPassword, false);
  }


  // Test for https://github.com/yugabyte/yugabyte-db/issues/2505.
  @Test
  public void testAlterOwnSuperuserStatusFails() throws Exception {
    thrown.expect(UnauthorizedException.class);
    thrown.expectMessage("Unauthorized. You aren't allowed to alter your own superuser status or " +
            "that of a role granted to you");
    cs.execute("ALTER ROLE cassandra WITH SUPERUSER = false");
  }

  // Test for https://github.com/yugabyte/yugabyte-db/issues/2505.
  @Test
  public void testAlterSuperuserStatusOfGrantedRoleFails() throws Exception {
    createRole(cs.getSession(), "parent", "", false, true, false);
    createRole(cs.getSession(), "grandparent", "", false, true, false);

    cs.execute("GRANT grandparent TO parent");
    cs.execute("GRANT parent TO cassandra");

    thrown.expect(UnauthorizedException.class);
    thrown.expectMessage("Unauthorized. You aren't allowed to alter your own superuser status or " +
            "that of a role granted to you");
    cs.execute("ALTER ROLE grandparent WITH SUPERUSER = false");
  }

  @Test
  public void testNotEmptyResourcesInSytemAuthRolePermissionsTable() throws Exception {
    createRole(cs.getSession(), anotherUsername, "", false, false, false);

    String canonicalResource = String.format("roles/%s", anotherUsername);
    List<String> expectedPermissions = Arrays.asList(ALTER, AUTHORIZE, DROP);
    // Test that we can see the permissions when we query system_auth.role_permissions.
    assertPermissionsGranted(cs.getSession(), "cassandra", canonicalResource, expectedPermissions);

    for (String permission : expectedPermissions) {
      revokePermissionNoSleep(permission, ROLE, anotherUsername, "cassandra");
    }

    // Verify the resource doesn't appear anymore.
    String stmt = String.format("SELECT permissions FROM system_auth.role_permissions " +
        "WHERE role = 'cassandra' and resource = '%s';", canonicalResource);
    List<Row> rows = cs.execute(stmt).all();
    assert(rows.isEmpty());
  }

  @Test
  public void testInheritedPermissions() throws Exception {
    String level0 = "level0";
    String level1 = "level1";
    String level2 = "level2";
    String level3_0 = "level3_0";
    String level3_1 = "level3_1";

    createRole(cs.getSession(), level0, password, true /* canLogin */,
        false /* isSuperuser */, false /* verifyConnectivity */);
    createRole(cs.getSession(), level1, "", false, false, false);
    createRole(cs.getSession(), level2, "", false, false, false);
    createRole(cs.getSession(), level3_0, "", false, false, false);
    createRole(cs.getSession(), level3_1, "", false, false, false);

    cs.execute(String.format("GRANT %s TO %s", level3_0, level2));
    cs.execute(String.format("GRANT %s TO %s", level3_1, level2));
    cs.execute(String.format("GRANT %s TO %s", level2, level1));
    cs.execute(String.format("GRANT %s TO %s", level1, level0));

    cs.execute(String.format("GRANT CREATE ON ALL KEYSPACES TO %s", level3_0));

    // Sleep to give the cache some time to be refreshed.
    Thread.sleep(TIME_SLEEP_MS);

    // Verify that level0 can create a keyspace since it has inherited that permissions from
    // level3_0.
    try (ClusterAndSession level0ClusterSession = connectWithCredentials(level0, password)) {
      level0ClusterSession.execute("CREATE KEYSPACE somekeyspace");

      // Grant CREATE ON ALL ROLES to level3_1 and verify that level0 role can create a role.
      cs.execute(String.format("GRANT CREATE ON ALL ROLES TO %s", level3_1));

      // Sleep to give the cache some time to be refreshed.
      Thread.sleep(TIME_SLEEP_MS);

      level0ClusterSession.execute("CREATE ROLE somerole");

      cs.execute(String.format("GRANT DROP ON ALL KEYSPACES TO %s", level3_1));

      // Sleep to give the cache some time to be refreshed.
      Thread.sleep(TIME_SLEEP_MS);

      // Verify again that level0 can create a keyspace since it is now inheriting two different
      // permissions on ALL KEYSPACES from two different roles.
      level0ClusterSession.execute("CREATE KEYSPACE somekeyspace2");
    }
  }

  // This test a fix for https://github.com/yugabyte/yugabyte-db/issues/4062.
  // The issue is that when different permissions are inherited from roles granted to another role,
  // it's possible that they might replace the permissions granted directly.
  // For example:
  // If role 'eng' has permission SELECT on table 'releases', and role 'john' has permission
  // MODIFY on the same table, and role 'eng' is granted to 'john', it's possible (depending on
  // the order the permissions are received) that permission SELECT will replace permission
  // MODIFY on table 'releases'.
  @Test
  public void testInheritedPermissionsDoNotOverrideGrantedPermissions() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), "employees", password, true /* canLogin */,
        false /* isSuperuser */, false /*verifyConnectivity */);
    createRole(cs.getSession(), "eng", password, true /* canLogin */,
        false /* isSuperuser */, false /*verifyConnectivity */);

    cs.execute(String.format("GRANT employees TO eng"));
    cs.execute(String.format("GRANT SELECT ON %s.%s TO eng", keyspace, table));

    createRole(cs.getSession(), "john", password, true, false, false);

    cs.execute(String.format("GRANT eng TO john"));
    cs.execute(String.format("GRANT MODIFY ON %s.%s TO john", keyspace, table));


    try (ClusterAndSession johnClusterSession = connectWithCredentials("john", password)) {
      // Sleep to give the cache some time to be refreshed.
      Thread.sleep(TIME_SLEEP_MS);

      // Verify that user 'john' can insert a record in the table.
      insertRow(johnClusterSession.getSession(), keyspace, table);
      johnClusterSession.execute(String.format("SELECT * FROM %s.%s", keyspace, table));
    }
  }

  public void testGrantAllGrantsCorrectPermissions() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    createRole(cs.getSession(), anotherUsername, "a", false, false, false);

    grantAllPermission(KEYSPACE, keyspace, username);
    assertPermissionsGranted(cs.getSession(), username, "data/" + keyspace,
        Arrays.asList(ALTER, AUTHORIZE, CREATE, DROP, MODIFY, SELECT));

    grantAllPermission(TABLE, String.format("%s.%s", keyspace, table), username);
    assertPermissionsGranted(cs.getSession(), username,
        String.format("data/%s/%s", keyspace, table),
        Arrays.asList(ALTER, AUTHORIZE, DROP, MODIFY, SELECT));

    grantAllPermission(ROLE, anotherUsername, username);
    assertPermissionsGranted(cs.getSession(), username, "roles/" + anotherUsername,
        Arrays.asList(ALTER, AUTHORIZE, DROP));

    grantPermissionOnAllKeyspaces(ALL, username);
    assertPermissionsGranted(cs.getSession(), username, "data",
        Arrays.asList(ALTER, AUTHORIZE, CREATE, DROP, MODIFY, SELECT));

    grantPermissionOnAllRoles(ALL, username);
    grantAllPermission(ROLE, anotherUsername, username);
    assertPermissionsGranted(cs.getSession(), username, "roles",
        Arrays.asList(ALTER, AUTHORIZE, CREATE, DESCRIBE, DROP));
  }

  private void testPermissionOnResourceFails(String permission, String resourceType,
      String resourceName, String receivingRole) throws Exception {
    thrown.expect(com.datastax.driver.core.exceptions.SyntaxError.class);
    thrown.expectMessage(
        "Resource type DataResource does not support any of the requested permissions");
    grantPermission(permission, resourceType, resourceName, receivingRole);
  }

  @Test
  public void testGrantDescribeOnKeyspaceFails() throws Exception {
    testPermissionOnResourceFails(DESCRIBE, KEYSPACE, keyspace, username);
  }

  @Test
  public void testGrantDescribeOnAllKeyspacesFails() throws Exception {
    testPermissionOnResourceFails(DESCRIBE, ALL_KEYSPACES, "", username);
  }

  @Test
  public void testGrantDescribeOnTableFails() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    testPermissionOnResourceFails(DESCRIBE, TABLE, String.format("%s.%s", keyspace, table),
        username);
  }

  @Test
  public void testGrantDescribeOnRoleFails() throws Exception {
    createRole(cs.getSession(), anotherUsername, "a", false, false, false);
    testPermissionOnResourceFails(DESCRIBE, ROLE, anotherUsername, username);
  }

  @Test
  public void testGrantCreateOnTableFails() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    testPermissionOnResourceFails(CREATE, TABLE, String.format("%s.%s", keyspace, table), username);
  }

  @Test
  public void testGrantCreateOnRoleFails() throws Exception {
    createRole(cs.getSession(), anotherUsername, "a", false, false, false);
    testPermissionOnResourceFails(CREATE, ROLE, username, anotherUsername);
  }

  @Test
  public void testGrantModifyOnRoleFails() throws Exception {
    createRole(cs.getSession(), anotherUsername, "a", false, false, false);
    testPermissionOnResourceFails(MODIFY, ROLE, username, anotherUsername);
  }

  @Test
  public void testGrantSelectOnRoleFails() throws Exception {
    createRole(cs.getSession(), anotherUsername, "a", false, false, false);
    testPermissionOnResourceFails(SELECT, ROLE, username, anotherUsername);
  }

  @Test
  public void testGrantModifyOnAllRoleFails() throws Exception {
    testPermissionOnResourceFails(MODIFY, ALL_ROLES, "", username);
  }

  @Test
  public void testGrantSelectOnAllRoleFails() throws Exception {
    testPermissionOnResourceFails(SELECT, ALL_ROLES, "", username);
  }

  @Test
  public void testCreateIndexWithCreateTablePermission() throws Exception {
    cs.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h)) " +
        "WITH transactions = { 'enabled' : true }", keyspace, table));
    cs.execute("USE " + keyspace);

    grantPermission(CREATE, KEYSPACE, keyspace, username);

    String createIndexStmt = String.format("CREATE INDEX order_by_v on %s.%s (v)",
        keyspace, table);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(createIndexStmt);
  }

  @Test
  public void testCreateIndexWithAlterTablePermission() throws Exception {
    cs.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h)) " +
        "WITH transactions = { 'enabled' : true }", keyspace, table));
    cs.execute("USE " + keyspace);
    grantPermission(ALTER, TABLE, table, username);

    String createIndexStmt = String.format("CREATE INDEX order_by_v on %s.%s (v)",
        keyspace, table);

    cs2.execute(createIndexStmt);
  }

  @Test
  public void testDropIndexWithWrongTablePermission() throws Exception {
    cs.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h)) " +
        "WITH transactions = { 'enabled' : true }", keyspace, table));
    cs.execute("USE " + keyspace);
    grantPermission(DROP, TABLE, table, username);

    String indexName = "drop_test_order_by_v_2";

    String createIndexStmt = String.format("CREATE INDEX %s on %s.%s (v)",
        indexName, keyspace, table);

    cs.execute(createIndexStmt);

    Thread.sleep(1000);

    String dropIndexStmt = String.format("DROP INDEX %s.%s", keyspace, indexName);
    thrown.expect(UnauthorizedException.class);
    thrown.expectMessage(String.format(
        "User %s has no ALTER permission on <table %s.%s> or any of its parents",
        username, keyspace, table));
    cs2.execute(dropIndexStmt);
  }

  @Test
  public void testDropIndexWithAlterTablePermission() throws Exception {
    cs.execute(String.format("CREATE TABLE %s.%s (h int, v int, PRIMARY KEY(h)) " +
        "WITH transactions = { 'enabled' : true }", keyspace, table));
    cs.execute("USE " + keyspace);
    grantPermission(ALTER, TABLE, table, username);

    String indexName = "drop_test_order_by_v_3";

    String createIndexStmt = String.format("CREATE INDEX %s on %s.%s (v)",
        indexName, keyspace, table);

    cs.execute(createIndexStmt);

    String dropIndexStmt = String.format("DROP INDEX %s.%s", keyspace, indexName);
    cs2.execute(dropIndexStmt);
  }

  @Test
  public void testDropTypeWithAllKeyspacesPermission() throws Exception {
    String typeName = "test_type";
    String typeName2 = typeName + "_2";

    LOG.info("Begin test");
    grantPermissionOnAllKeyspaces(CREATE, username);

    cs2.execute(String.format("CREATE KEYSPACE %s", anotherKeyspace));
    cs2.execute(String.format("CREATE TYPE %s.%s (id TEXT)", anotherKeyspace, typeName));
    // Type owner must be able to drop own type.
    cs2.execute(String.format("DROP TYPE %s.%s", anotherKeyspace, typeName));

    cs2.execute("USE " + anotherKeyspace);
    cs2.execute(String.format("CREATE TYPE %s (id INT)", typeName2));
    cs2.execute(String.format("DROP TYPE %s", typeName2));

    LOG.info("End test");
  }

  @Test
  public void testCreateTypeWithKeyspacePermission() throws Exception {
    String typeName = "test_type";
    String typeName2 = typeName + "_2";

    LOG.info("Begin test");
    cs.execute(String.format("CREATE KEYSPACE %s", anotherKeyspace));

    try {
      cs2.execute(String.format("CREATE TYPE %s.%s (id TEXT)", anotherKeyspace, typeName));
      fail("CREATE TYPE works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }
    cs2.execute("USE " + anotherKeyspace);
    try {
      cs2.execute(String.format("CREATE TYPE %s (id INT)", typeName2));
      fail("CREATE TYPE works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }

    grantPermission(CREATE, KEYSPACE, anotherKeyspace, username);
    cs2.execute(String.format("CREATE TYPE %s.%s (id TEXT)", anotherKeyspace, typeName));
    cs2.execute(String.format("CREATE TYPE %s (id INT)", typeName2));

    try {
      cs2.execute(String.format("DROP TYPE %s.%s", anotherKeyspace, typeName));
      fail("DROP TYPE works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }
    try {
      cs2.execute(String.format("DROP TYPE %s", typeName2));
      fail("DROP TYPE works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }

    grantPermission(DROP, KEYSPACE, anotherKeyspace, username);
    cs2.execute(String.format("DROP TYPE %s.%s", anotherKeyspace, typeName));
    cs2.execute(String.format("DROP TYPE %s", typeName2));

    LOG.info("End test");
  }

  private void internalTestPreparedCreateDropType(String resourceType,
                                                  String resource) throws Exception {
    String typeName = "test_type";
    String typeName2 = typeName + "_2";
    cs.execute(String.format("CREATE KEYSPACE %s", anotherKeyspace));

    LOG.info("Begin test");

    // Prepare and execute statements.
    String createTypeStmt = String.format("CREATE TYPE %s.%s (id TEXT)", anotherKeyspace, typeName);
    grantPermission(CREATE, resourceType, resource, username);
    PreparedStatement prepCreateStmt = cs2.prepare(createTypeStmt);
    revokePermission(CREATE, resourceType, resource, username);
    try {
      cs2.execute(prepCreateStmt.bind());
      fail("Prepared CREATE TYPE works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }

    grantPermission(CREATE, resourceType, resource, username);
    cs2.execute(prepCreateStmt.bind());

    String dropTypeStmt = String.format("DROP TYPE %s.%s", anotherKeyspace, typeName);
    grantPermission(DROP, resourceType, resource, username);
    PreparedStatement prepDropStmt = cs2.prepare(dropTypeStmt);
    revokePermission(DROP, resourceType, resource, username);
    try {
      cs2.execute(prepDropStmt.bind());
      fail("Prepared DROP TYPE works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }

    grantPermission(DROP, resourceType, resource, username);
    cs2.execute(prepDropStmt.bind());

    cs2.execute("USE " + anotherKeyspace);

    // Prepare and execute statements.
    String createTypeStmt2 = String.format("CREATE TYPE %s (id TEXT)", typeName2);
    PreparedStatement prepCreateStmt2 = cs2.prepare(createTypeStmt2);
    revokePermission(CREATE, resourceType, resource, username);
    try {
      cs2.execute(prepCreateStmt2.bind());
      fail("Prepared CREATE TYPE works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }

    grantPermission(CREATE, resourceType, resource, username);
    cs2.execute(prepCreateStmt2.bind());

    String dropTypeStmt2 = String.format("DROP TYPE %s", typeName2);
    PreparedStatement prepDropStmt2 = cs2.prepare(dropTypeStmt2);
    revokePermission(DROP, resourceType, resource, username);
    try {
      cs2.execute(prepDropStmt2.bind());
      fail("Prepared DROP TYPE works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }

    grantPermission(DROP, resourceType, resource, username);
    cs2.execute(prepDropStmt2.bind());

    LOG.info("End test");
  }

  @Test
  public void testPreparedCreateDropTypeWithAllKeyspacesPermission() throws Exception {
    // Test prepared statements with ALL_KEYSPACES permissions.
    internalTestPreparedCreateDropType(ALL_KEYSPACES, "");
  }

  @Test
  public void testPreparedCreateDropTypeWithKeyspacePermission() throws Exception {
    // Test prepared statements with used keyspace permissions.
    internalTestPreparedCreateDropType(KEYSPACE, anotherKeyspace);
  }

  @Test
  public void testExplain() throws Exception {
    LOG.info("Begin test");

    cs.execute(String.format("CREATE TABLE %s.%s (auth_id text, PRIMARY KEY(auth_id))",
                            keyspace, table));
    String explainStmt = String.format("EXPLAIN SELECT * FROM %s.%s WHERE auth_id=''",
                                       keyspace, table);
    try {
      cs2.execute(explainStmt);
      fail("EXPLAIN SELECT works without permissions");
    } catch (com.datastax.driver.core.exceptions.UnauthorizedException e) {
      LOG.info("Expected exception:", e);
    }

    grantPermission(SELECT, TABLE, keyspace + '.' + table, username);
    cs2.execute(explainStmt);

    LOG.info("End test");
  }

  protected static enum GrantRevoke {
    GRANT, REVOKE
  }
}
