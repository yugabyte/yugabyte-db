// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.apache.commons.lang3.StringUtils;

import org.hamcrest.CoreMatchers;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.Before;

import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.SystemUtil;
import org.yb.pgsql.BasePgSQLTest;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.YBParameterizedTestRunner;
import org.yb.YBTestRunner;
import com.yugabyte.util.PSQLException;


@RunWith(value = YBTestRunner.class)
public class TestPasswordAuth extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPasswordAuth.class);
  private static final String INCORRECT_PASSWORD_AUTH_MSG =
    "password authentication failed for user";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    // The MD5 auth method in an hba file allows authentication through
    // both MD5 and SCRAM methods.
    builder.addCommonTServerFlag("ysql_hba_conf_csv",
    "\"host all md5_user all md5\","    +
    "\"host all scram_user all md5 \"," +
    "\"host all all all trust\"");
  }

  // Setup users with MD5 and SCRAM based password
  // auth before running the tests.
  @Before
  public void setupAuthUsers() {
    // Create new users with MD5 & SCRAM credentials
    try (Connection connection = getConnectionBuilder().connect();
    Statement statement = connection.createStatement()) {

      for(AuthType authType : AuthType.values()) {
        statement.execute("SET password_encryption='" + authType.type + "'");
        statement.execute("CREATE USER " + authType.username + " WITH PASSWORD '"
          + authType.password + "'");
      }
      LOG.info("Created auth test users");

    } catch (Exception e) {
      LOG.error("", e);
      fail ("Failed to setup users");
    }
  }

  // Try logging in with the given auth type.
  // Basic unit for this test.
  private void tryLogin(AuthType authType, boolean expectFail) throws Exception {
    String authString = authType.toString() + " password authentication";
    ConnectionBuilder loginConnBldr = getConnectionBuilder()
      .withUser(authType.username)
      .withPassword(authType.getPassword(expectFail));

    //  login.
    try (Connection connection = loginConnBldr.connect()) {
      // No-op if expected success.
      if(expectFail)
        fail(authString + " succeeded with wrong password");
    } catch (PSQLException e) {
      // No-op if expected failure.
      if(!expectFail) {
        if (StringUtils.containsIgnoreCase(e.getMessage(),
            INCORRECT_PASSWORD_AUTH_MSG)) {
          fail(authString + " failed with correct password");
        } else {
          fail(authString + " unexpected error message:'%s'" + e.getMessage());
        }
      }
    }
  }

  @Test
  public void testPasswordAuthLogin() throws Exception {
    for(AuthType type : AuthType.values()) {
      tryLogin(type, false);
      tryLogin(type, true);
    }
  }

  @Test
  public void testDefaultPasswordEncryptionIsSCRAM() throws Exception {
    try (Connection connection = getConnectionBuilder().connect();
         Statement statement = connection.createStatement()) {

      // NOTE: Even though TestPasswordAuth configures MD5 authentication in HBA config,
      // this only affects client authentication methods, NOT the password_encryption setting.
      // The initdb password_encryption logic only triggers when explicit --auth-local/--auth-host
      // parameters are passed to initdb, which doesn't happen here.
      try (ResultSet rs = statement.executeQuery(
          "SELECT setting FROM pg_settings WHERE name='password_encryption'")) {
        assertTrue("Expected password_encryption setting", rs.next());
        assertEquals("password_encryption should remain default scram-sha-256 ",
                     "scram-sha-256", rs.getString(1));
      }

      // Create a user with default settings and verify it uses SCRAM (the actual default)
      statement.execute("CREATE ROLE test_default_scram_user PASSWORD 'testpass'");

      try (ResultSet rs = statement.executeQuery(
          "SELECT LEFT(rolpassword, 13) FROM pg_authid WHERE rolname='test_default_scram_user'")) {
        assertTrue("Expected user password entry", rs.next());
        assertEquals("User created with default settings should use SCRAM-SHA-256 (true default)",
                     "SCRAM-SHA-256", rs.getString(1));
      }

      statement.execute("DROP ROLE test_default_scram_user");
    }
  }

  @Test
  public void testYugabyteUserHasSCRAMPassword() throws Exception {
    try (Connection connection = getConnectionBuilder().connect();
         Statement statement = connection.createStatement()) {

      // NOTE: Even though TestPasswordAuth configures MD5 authentication in HBA config,
      // this only affects client authentication methods, NOT initdb's password creation.
      // The yugabyte user is created with the default password_encryption setting (scram-sha-256)
      // because HBA config doesn't trigger initdb's MD5 password_encryption logic.
      try (ResultSet rs = statement.executeQuery(
          "SELECT LEFT(rolpassword, 13) FROM pg_authid WHERE rolname='yugabyte'")) {
        assertTrue("Expected yugabyte user password entry", rs.next());
        assertEquals("yugabyte user should have default SCRAM-SHA-256 password " +
                     "(HBA config doesn't affect initdb)",
                     "SCRAM-SHA-256", rs.getString(1));
      }

      LOG.info("Verified: yugabyte user has SCRAM-SHA-256 password format (true default behavior)");
    }
  }

  @Test
  public void testMixedPasswordTypesCompat() throws Exception {
    try (Connection connection = getConnectionBuilder().connect();
         Statement statement = connection.createStatement()) {

      // Test backward compatibility: create users with both MD5 and SCRAM passwords
      // and verify both formats are stored correctly

      // Create MD5 user (legacy format)
      statement.execute("SET password_encryption = 'md5'");
      statement.execute("CREATE ROLE test_compat_md5 PASSWORD 'legacypass'");

      // Create SCRAM user (new default format)
      statement.execute("SET password_encryption = 'scram-sha-256'");
      statement.execute("CREATE ROLE test_compat_scram PASSWORD 'newpass'");

      // Verify password storage formats
      try (ResultSet rs = statement.executeQuery(
          "SELECT rolname, rolpassword FROM pg_authid " +
          "WHERE rolname IN ('test_compat_md5', 'test_compat_scram') ORDER BY rolname")) {

        // MD5 user verification
        assertTrue("Expected MD5 user", rs.next());
        assertEquals("MD5 user name", "test_compat_md5", rs.getString(1));
        String md5Pass = rs.getString(2);
        assertTrue("MD5 password should start with 'md5'", md5Pass.startsWith("md5"));
        assertEquals("MD5 password should be 35 chars", 35, md5Pass.length());
        LOG.info("MD5 user password format verified: " + md5Pass.substring(0, 8) + "...");

        // SCRAM user verification
        assertTrue("Expected SCRAM user", rs.next());
        assertEquals("SCRAM user name", "test_compat_scram", rs.getString(1));
        String scramPass = rs.getString(2);
        assertTrue("SCRAM password should start with 'SCRAM-SHA-256'",
                   scramPass.startsWith("SCRAM-SHA-256$4096:"));
        LOG.info("SCRAM user password format verified: " + scramPass.substring(0, 20) + "...");
      }

      LOG.info("Backward compatibility verified: Both MD5 and SCRAM password formats work");

      statement.execute("DROP ROLE test_compat_md5");
      statement.execute("DROP ROLE test_compat_scram");
    }
  }

  private enum AuthType {
    MD5("md5", "md5_user", "password"),
    SCRAM("scram-sha-256", "scram_user", "password");

    AuthType(String type, String username, String password) {
      this.type = type;
      this.username = username;
      this.password = password;
      this.falsePassword = password + "123";
    }

    public final String type;
    public final String username;
    public final String password;
    public final String falsePassword;
    public String getPassword(boolean expectFail) {
      return (expectFail ? this.falsePassword : this.password);
    }
  }
}
