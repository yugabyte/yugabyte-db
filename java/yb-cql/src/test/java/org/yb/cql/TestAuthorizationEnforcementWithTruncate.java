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

import java.util.Arrays;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.exceptions.UnauthorizedException;

// This test enables ycql_require_drop_privs_for_truncate flag.
@RunWith(value=YBTestRunner.class)
public class TestAuthorizationEnforcementWithTruncate extends TestAuthorizationEnforcement {
  private static final Logger LOG =
    LoggerFactory.getLogger(org.yb.cql.TestAuthorizationEnforcementWithTruncate.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ycql_require_drop_privs_for_truncate", Boolean.toString(true));
    return flagMap;
  }

  @Test
  public void testTruncateStatementWithWrongPermissionsOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(DROP, CREATE, DESCRIBE), TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithWrongPermissionsOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    grantAllPermissionsExcept(Arrays.asList(DESCRIBE, DROP), KEYSPACE, keyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithDropPermissionOnDifferentTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createTableAndInsertRecord(cs.getSession(), keyspace, anotherTable);
    grantPermission(DROP, TABLE, anotherTable, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithDropPermissionOnDifferentKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createKeyspaceAndVerify(cs.getSession(), anotherKeyspace);
    grantPermission(DROP, KEYSPACE, anotherKeyspace, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStamentWithDropPermissionOnTableToDifferentRole() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(DROP, TABLE, table, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithDropPermissionOnKeyspaceToDifferentRole()
      throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);

    createRole(cs.getSession(), anotherUsername, password, true, false, false);
    grantPermission(DROP, KEYSPACE, keyspace, anotherUsername);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithModifyPermissionOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantPermission(MODIFY, TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithDropPermissionOnTable() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantPermission(DROP, TABLE, table, username);
    truncateTableAndVerify(cs2.getSession(), keyspace, table);
  }

  @Test
  public void testTruncateStatementWithModifyPermissionOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantPermission(MODIFY, KEYSPACE, keyspace, username);
    thrown.expect(UnauthorizedException.class);
    cs2.execute(String.format("TRUNCATE %s.%s", keyspace, table));
  }

  @Test
  public void testTruncateStatementWithDropPermissionOnKeyspace() throws Exception {
    createTableAndInsertRecord(cs.getSession(), keyspace, table);
    grantPermission(DROP, KEYSPACE, keyspace, username);
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

  @Test
  public void testTruncateTableWithModifyPermission() throws Exception {
    createTableAndVerify(cs.getSession(), keyspace, table);
    grantPermission(MODIFY, TABLE, table, username);
    grantPermission(DROP, TABLE, table, username);

    // Prepare and excecute statement.
    String truncateStmt = String.format("TRUNCATE %s.%s", keyspace, table);
    PreparedStatement stmt = cs2.prepare(truncateStmt);
    cs2.execute(stmt.bind());

    revokePermission(DROP, TABLE, table, username);

    thrown.expect(UnauthorizedException.class);
    cs2.execute(stmt.bind());
  }
}
