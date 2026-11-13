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
package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

/**
 * yb_password_validity cases that need a value in the configuration file (ysql_pg_conf_csv). The
 * pg_regress test yb.orig.password_valid_until covers the rest; its cluster leaves the file value at
 * the default, 0.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgPasswordValidity extends BasePgSQLTest {
  private static final int FILE_VALIDITY_DAYS = 50;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // The parameter is in minutes.
    appendToYsqlPgConf(flagMap, "yb_password_validity=" + FILE_VALIDITY_DAYS * 24 * 60);
    return flagMap;
  }

  /**
   * A role without a setting of its own gets the file's value, both from a session that has the
   * file's value and from a session whose own setting replaced it. A role exempted by its own
   * setting gets no expiry.
   */
  @Test
  public void testConfigFileValueForAnotherRole() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE ROLE pv_admin LOGIN CREATEROLE");
      stmt.execute("ALTER ROLE pv_admin SET yb_password_validity = 0");
      stmt.execute("CREATE ROLE pv_target LOGIN");
      stmt.execute("CREATE ROLE pv_exempt LOGIN VALID UNTIL 'infinity'");
      stmt.execute("ALTER ROLE pv_exempt SET yb_password_validity = 0");

      assertEquals(FILE_VALIDITY_DAYS + "d", showValidity(stmt));
      stmt.execute("ALTER ROLE pv_target PASSWORD 'secret'");
      stmt.execute("ALTER ROLE pv_exempt PASSWORD 'secret'");
      assertEquals(FILE_VALIDITY_DAYS + " days", expiry(stmt, "pv_target"));
      assertEquals("infinity", expiry(stmt, "pv_exempt"));
      stmt.execute("ALTER ROLE pv_target VALID UNTIL 'infinity'");
    }

    try (Connection admin = getConnectionBuilder().withUser("pv_admin").connect();
         Statement stmt = admin.createStatement()) {
      assertEquals("0", showValidity(stmt));
      stmt.execute("ALTER ROLE pv_target PASSWORD 'secret2'");
      stmt.execute("CREATE ROLE pv_created LOGIN PASSWORD 'secret'");
    }

    try (Statement stmt = connection.createStatement()) {
      assertEquals(FILE_VALIDITY_DAYS + " days", expiry(stmt, "pv_target"));
      assertEquals(FILE_VALIDITY_DAYS + " days", expiry(stmt, "pv_created"));
    }
  }

  private static String showValidity(Statement stmt) throws SQLException {
    return getSingleRow(stmt, "SHOW yb_password_validity").getString(0);
  }

  /**
   * Days until the role's password expires. The policy stamps the current time plus whole days,
   * so this rounds to that number.
   */
  private static String expiry(Statement stmt, String role) throws SQLException {
    return getSingleRow(stmt,
        "SELECT CASE WHEN rolvaliduntil IS NULL THEN 'none'" +
        " WHEN isfinite(rolvaliduntil)" +
        " THEN round(extract(epoch FROM rolvaliduntil - now()) / 86400) || ' days'" +
        " ELSE rolvaliduntil::text END" +
        " FROM pg_authid WHERE rolname = '" + role + "'").getString(0);
  }
}
