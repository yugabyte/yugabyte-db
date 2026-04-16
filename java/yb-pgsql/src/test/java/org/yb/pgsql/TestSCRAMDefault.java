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
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import com.yugabyte.util.PSQLException;

/**
 * Tests for SCRAM-SHA-256 as the default password encryption method.
 * This test validates YugabyteDB's change from MD5 to SCRAM-SHA-256 as the default.
 */
@RunWith(value = YBTestRunner.class)
public class TestSCRAMDefault extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestSCRAMDefault.class);

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
  }

  @Override
  protected Connection createTestRole() throws Exception {
    // No authentication for initial setup
    Connection initialConnection;
    try {
      initialConnection = getConnectionBuilder().withUser(DEFAULT_PG_USER).connect();
    } catch (PSQLException e) {
      if (e.getMessage().contains("authentication") || e.getMessage().contains("password")) {
        initialConnection = getConnectionBuilder().withUser(DEFAULT_PG_USER)
                                                 .withPassword(DEFAULT_PG_PASS)
                                                 .connect();
      } else {
        throw e;
      }
    }

    try (Statement statement = initialConnection.createStatement()) {
      statement.execute(
          String.format("CREATE ROLE %s SUPERUSER CREATEROLE CREATEDB BYPASSRLS LOGIN "
                        + "PASSWORD '%s'", TEST_PG_USER, TEST_PG_PASS));
    }
    initialConnection.close();

    return getConnectionBuilder().withPassword(TEST_PG_PASS).connect();
  }

  @Override
  public void verifyClusterAcceptsPGConnections(long timeoutMs) throws Exception {
    LOG.info("Waiting for cluster to accept pg connections");
    TestUtils.waitFor(() -> {
        try {
          getConnectionBuilder().withUser(DEFAULT_PG_USER).connect().close();
        } catch (PSQLException e) {
          try {
            getConnectionBuilder()
              .withUser(DEFAULT_PG_USER)
              .withPassword(DEFAULT_PG_PASS)
              .connect()
              .close();
          } catch (Exception inner) {
            return false;
          }
        } catch (Exception e) {
          return false;
        }
        return true;
      }, timeoutMs);
    LOG.info("done Waiting for cluster to accept pg connections");
  }

  private void tryLogin(String username, String password, boolean expectFail, String scenario)
      throws Exception {
    LOG.info("Testing login scenario: " + scenario + " (expecting " +
             (expectFail ? "failure" : "success") + ")");

    ConnectionBuilder loginConnBuilder = getConnectionBuilder()
        .withUser(username)
        .withPassword(password);

    try (Connection connection = loginConnBuilder.connect()) {
      if (expectFail) {
        fail(scenario + " - Login succeeded with " +
             (expectFail ? "wrong" : "correct") + " password, but expected failure");
      }
      LOG.info(scenario + " - Login succeeded as expected");
    } catch (PSQLException e) {
      if (!expectFail) {
        if (e.getMessage().toLowerCase().contains("password authentication failed")) {
          fail(scenario + " - Login failed with correct password: " + e.getMessage());
        } else {
          fail(scenario + " - Unexpected login error: " + e.getMessage());
        }
      }
      LOG.info(scenario + " - Login failed as expected: " + e.getMessage());
    }
  }

  private void enableBasicAuthentication() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_enable_auth", "true");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);
  }

  @Test
  public void testPasswordEncryptionDefaultIsSCRAM() throws Exception {
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {

      // Test 1: Verify password_encryption defaults to 'scram-sha-256'
      ResultSet rs = stmt.executeQuery(
          "SELECT boot_val FROM pg_settings WHERE name = 'password_encryption'");
      assertTrue("Expected result for password_encryption default", rs.next());
      assertEquals("Expected default password encryption", "scram-sha-256",
          rs.getString(1));
      rs.close();

      // Test 2: Verify current setting after RESET is also 'scram-sha-256'
      stmt.execute("RESET password_encryption");
      rs = stmt.executeQuery("SHOW password_encryption");
      assertTrue("Expected result for current password_encryption", rs.next());
      assertEquals("Expected current password encryption", "scram-sha-256",
          rs.getString(1));
      rs.close();
    }
  }

  @Test
  public void testDefaultUserCreationUsesSCRAM() throws Exception {
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {

      // Test 3: Create user with default settings should use SCRAM-SHA-256
      String testUserName = "test_scram_default_user_java";
      stmt.execute("DROP ROLE IF EXISTS " + testUserName);
      stmt.execute("CREATE ROLE " + testUserName + " LOGIN PASSWORD 'testpass123'");

      // Verify the password is stored as SCRAM-SHA-256
      ResultSet rs = stmt.executeQuery(
          "SELECT LEFT(rolpassword, 13) FROM pg_authid WHERE rolname = '" +
          testUserName + "'");
      assertTrue("Expected result for user password format", rs.next());
      assertEquals("Expected SCRAM password prefix", "SCRAM-SHA-256",
          rs.getString(1));
      rs.close();

      stmt.execute("DROP ROLE " + testUserName);
    }
  }

  @Test
  public void testSCRAMAndMD5UsersCanAuthenticate() throws Exception {
    String scramUser = "test_scram_auth_user";
    String md5User = "test_md5_auth_user";
    String scramPassword = "scrampass123";
    String md5Password = "md5pass123";

    try {
      // First enable authentication
      enableBasicAuthentication();

      // Then create users AFTER cluster restart (so they exist in the new cluster)
      try (Connection conn = getConnectionBuilder().withPassword(TEST_PG_PASS).connect();
           Statement stmt = conn.createStatement()) {

        stmt.execute("DROP ROLE IF EXISTS " + scramUser);
        stmt.execute("DROP ROLE IF EXISTS " + md5User);

        // Test 4: Create SCRAM user (default behavior)
        stmt.execute("CREATE ROLE " + scramUser + " LOGIN PASSWORD '" + scramPassword + "'");

        // Test 5: Create MD5 user (explicit setting)
        stmt.execute("SET password_encryption = 'md5'");
        stmt.execute("CREATE ROLE " + md5User + " LOGIN PASSWORD '" + md5Password + "'");
        stmt.execute("RESET password_encryption");

        // Ensure yugabyte user has SCRAM password for cluster restarts
        stmt.execute("SET password_encryption = 'scram-sha-256'");
        stmt.execute("ALTER USER yugabyte PASSWORD 'yugabyte'");
        stmt.execute("RESET password_encryption");

        // Verify password formats
        ResultSet rs = stmt.executeQuery(
            "SELECT rolname, LEFT(rolpassword, 13) as password_type " +
            "FROM pg_authid WHERE rolname IN ('" + scramUser + "', '" + md5User +
            "') ORDER BY rolname");

        assertTrue("Expected result for MD5 user", rs.next());
        assertEquals("Expected MD5 user name", md5User, rs.getString("rolname"));
        assertTrue("Expected MD5 password prefix",
            rs.getString("password_type").startsWith("md5"));

        assertTrue("Expected result for SCRAM user", rs.next());
        assertEquals("Expected SCRAM user name", scramUser, rs.getString("rolname"));
        assertEquals("Expected SCRAM password prefix", "SCRAM-SHA-256",
            rs.getString("password_type"));

        rs.close();
      }

      tryLogin(scramUser, scramPassword, false, "SCRAM user with correct password");
      tryLogin(scramUser, "wrongpassword", true, "SCRAM user with wrong password");

      tryLogin(md5User, md5Password, false, "MD5 user with correct password");
      tryLogin(md5User, "wrongpassword", true, "MD5 user with wrong password");

      // Test 6: Both users should be able to authenticate
      try (Connection conn = getConnectionBuilder().withPassword(TEST_PG_PASS).connect();
           Statement stmt = conn.createStatement()) {
        verifyPasswordIsValid(stmt, scramUser);
        verifyPasswordIsValid(stmt, md5User);
      }

    } finally {
      restartClusterWithFlags(Collections.emptyMap(), Collections.emptyMap());
      try (Connection conn = getConnectionBuilder().connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("DROP ROLE IF EXISTS " + scramUser);
        stmt.execute("DROP ROLE IF EXISTS " + md5User);
      }
    }
  }

  @Test
  public void testBackwardCompatibilitySettings() throws Exception {
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {

      // Test 7: Verify we can still set password_encryption to 'md5'
      stmt.execute("SET password_encryption = 'md5'");
      ResultSet rs = stmt.executeQuery("SHOW password_encryption");
      assertTrue("Expected result for MD5 setting", rs.next());
      assertEquals("Expected MD5 setting", "md5", rs.getString(1));
      rs.close();

      // Test 8: Verify we can set it back to 'scram-sha-256'
      stmt.execute("SET password_encryption = 'scram-sha-256'");
      rs = stmt.executeQuery("SHOW password_encryption");
      assertTrue("Expected result for SCRAM setting", rs.next());
      assertEquals("Expected SCRAM setting", "scram-sha-256", rs.getString(1));
      rs.close();

      // Test 9: Verify invalid values are rejected
      try {
        stmt.execute("SET password_encryption = 'invalid'");
        fail("Expected invalid password_encryption value to be rejected");
      } catch (Exception e) {
        assertTrue("Expected error message about invalid value",
            e.getMessage().contains("invalid value"));
      }

      stmt.execute("RESET password_encryption");
    }
  }

  @Test
  public void testMixedPasswordTypes() throws Exception {
    String[] testUsers = {"mixed_test_user1", "mixed_test_user2", "mixed_test_user3"};
    String[] testPasswords = {"pass1", "pass2", "pass3"};

    try {
      // First enable authentication
      enableBasicAuthentication();

      // Then create users AFTER cluster restart (so they exist in the new cluster)
      try (Connection conn = getConnectionBuilder().withPassword(TEST_PG_PASS).connect();
           Statement stmt = conn.createStatement()) {

        for (String user : testUsers) {
          stmt.execute("DROP ROLE IF EXISTS " + user);
        }

        // Test 10: Create users with different password encryption settings
        // User 1: Default (should be SCRAM)
        stmt.execute("CREATE ROLE " + testUsers[0] + " LOGIN PASSWORD '" + testPasswords[0] + "'");

        // User 2: Explicit MD5
        stmt.execute("SET password_encryption = 'md5'");
        stmt.execute("CREATE ROLE " + testUsers[1] + " LOGIN PASSWORD '" + testPasswords[1] + "'");

        // User 3: Back to SCRAM
        stmt.execute("SET password_encryption = 'scram-sha-256'");
        stmt.execute("CREATE ROLE " + testUsers[2] + " LOGIN PASSWORD '" + testPasswords[2] + "'");

        stmt.execute("RESET password_encryption");

        ResultSet rs = stmt.executeQuery(
            "SELECT rolname, " +
            "  CASE " +
            "    WHEN rolpassword LIKE 'SCRAM-SHA-256%' THEN 'SCRAM-SHA-256' " +
            "    WHEN rolpassword LIKE 'md5%' THEN 'md5' " +
            "    ELSE 'unknown' " +
            "  END as password_type " +
            "FROM pg_authid " +
            "WHERE rolname IN ('" + String.join("', '", testUsers) +
            "') ORDER BY rolname");

        // Verify user1 (default - should be SCRAM)
        assertTrue("Expected result for mixed_test_user1", rs.next());
        assertEquals("Expected user1 name", testUsers[0], rs.getString("rolname"));
        assertEquals("Expected user1 password type", "SCRAM-SHA-256",
            rs.getString("password_type"));

        // Verify user2 (explicit MD5)
        assertTrue("Expected result for mixed_test_user2", rs.next());
        assertEquals("Expected user2 name", testUsers[1], rs.getString("rolname"));
        assertEquals("Expected user2 password type", "md5",
            rs.getString("password_type"));

        // Verify user3 (explicit SCRAM)
        assertTrue("Expected result for mixed_test_user3", rs.next());
        assertEquals("Expected user3 name", testUsers[2], rs.getString("rolname"));
        assertEquals("Expected user3 password type", "SCRAM-SHA-256",
            rs.getString("password_type"));

        rs.close();
      }

      for (int i = 0; i < testUsers.length; i++) {
        String passwordType = (i == 1) ? "MD5" : "SCRAM";
        tryLogin(testUsers[i], testPasswords[i], false,
                 passwordType + " user (" + testUsers[i] + ") with correct password");
        tryLogin(testUsers[i], "wrongpass" + i, true,
                 passwordType + " user (" + testUsers[i] + ") with wrong password");
      }

      tryLogin(testUsers[0], testPasswords[0], false, "SCRAM user1 success");
      tryLogin(testUsers[1], testPasswords[1], false, "MD5 user2 success");
      tryLogin(testUsers[2], testPasswords[2], false, "SCRAM user3 success");

      tryLogin(testUsers[0], "badpass1", true, "SCRAM user1 failure");
      tryLogin(testUsers[1], "badpass2", true, "MD5 user2 failure");
      tryLogin(testUsers[2], "badpass3", true, "SCRAM user3 failure");

    } finally {
      restartClusterWithFlags(Collections.emptyMap(), Collections.emptyMap());
      try (Connection conn = getConnectionBuilder().connect();
           Statement stmt = conn.createStatement()) {
        for (String user : testUsers) {
          stmt.execute("DROP ROLE IF EXISTS " + user);
        }
      }
    }
  }

  @Test
  public void testAuthMethodSelectionWithFlagsAndYsqlAuthMethod() throws Exception {

    String md5User = "test_auth_method_md5";
    String scramUser = "test_auth_method_scram";
    String password = "authtest123";

    try {
      // Test with openssl_require_fips=true (should force SCRAM-SHA-256 auth method)
      Map<String, String> tserverFlags = new HashMap<>();
      tserverFlags.put("openssl_require_fips", "true");
      tserverFlags.put("ysql_enable_auth", "true");
      restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

      // Note: FIPS mode disables MD5 password authentication, so we can only create SCRAM users
      try (Connection conn = getConnectionBuilder().withPassword(TEST_PG_PASS).connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("CREATE ROLE " + scramUser + " LOGIN PASSWORD '" + password + "'");

        // Try to create MD5 user
        try {
          stmt.execute("SET password_encryption = 'md5'");
          stmt.execute("CREATE ROLE " + md5User + " LOGIN PASSWORD '" + password + "'");
        } catch (Exception e) {
          LOG.info("MD5 user creation failed as expected in FIPS mode: " + e.getMessage());
          // Create the MD5 user with SCRAM password instead for testing
          stmt.execute("SET password_encryption = 'scram-sha-256'");
          stmt.execute("CREATE ROLE " + md5User + " LOGIN PASSWORD '" + password + "'");
        }

        stmt.execute("RESET password_encryption");
      }

      tryLogin(scramUser, password, false, "SCRAM user with FIPS enabled (should succeed)");

      // In FIPS mode, the md5User was created with SCRAM password (since MD5 is disabled)
      // So it should also succeed
      tryLogin(md5User, password, false,
               "MD5-named user with FIPS enabled (should succeed - created with SCRAM password)");

      // Test with openssl_require_fips=false and ysql_auth_method="scram-sha-256"
      tserverFlags.clear();
      tserverFlags.put("openssl_require_fips", "false");
      tserverFlags.put("ysql_auth_method", "scram-sha-256");
      tserverFlags.put("ysql_enable_auth", "true");
      restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

      try (Connection conn = getConnectionBuilder().withPassword(TEST_PG_PASS).connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("SET password_encryption = 'md5'");
        stmt.execute("CREATE ROLE " + md5User + " LOGIN PASSWORD '" + password + "'");
        stmt.execute("SET password_encryption = 'scram-sha-256'");
        stmt.execute("CREATE ROLE " + scramUser + " LOGIN PASSWORD '" + password + "'");
        stmt.execute("RESET password_encryption");
      }

      // SCRAM user should succeed (SCRAM password + SCRAM auth method)
      tryLogin(scramUser, password, false,
               "SCRAM user with explicit scram-sha-256 method (should succeed)");

      // MD5 user should fail (MD5 password incompatible with SCRAM auth method)
      tryLogin(md5User, password, true,
               "MD5 user with explicit scram-sha-256 method (should fail - MD5 incompatible)");

      // Test with openssl_require_fips=false and ysql_auth_method="md5"
      tserverFlags.clear();
      tserverFlags.put("openssl_require_fips", "false");
      tserverFlags.put("ysql_auth_method", "md5");
      tserverFlags.put("ysql_enable_auth", "true");
      restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

      // Recreate users after restart
      try (Connection conn = getConnectionBuilder().withPassword(TEST_PG_PASS).connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("SET password_encryption = 'md5'");
        stmt.execute("CREATE ROLE " + md5User + " LOGIN PASSWORD '" + password + "'");
        stmt.execute("SET password_encryption = 'scram-sha-256'");
        stmt.execute("CREATE ROLE " + scramUser + " LOGIN PASSWORD '" + password + "'");
        stmt.execute("RESET password_encryption");
      }

      tryLogin(scramUser, password, false,
               "SCRAM user with explicit md5 method (should succeed - MD5 auth compatible)");

      tryLogin(md5User, password, false, "MD5 user with explicit md5 method (should succeed)");

    } finally {
      restartClusterWithFlags(Collections.emptyMap(), Collections.emptyMap());

      try (Connection conn = getConnectionBuilder().connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("DROP ROLE IF EXISTS " + md5User);
        stmt.execute("DROP ROLE IF EXISTS " + scramUser);
      }
    }
  }

  /**
   * Helper method to verify a password entry is properly formatted
   */
  private void verifyPasswordIsValid(Statement stmt, String username)
      throws Exception {
    ResultSet rs = stmt.executeQuery(
        "SELECT rolpassword FROM pg_authid WHERE rolname = '" + username + "'");
    assertTrue("Expected password entry for user " + username, rs.next());
    String password = rs.getString(1);
    assertTrue("Expected non-empty password for user " + username,
        password != null && !password.isEmpty());
    assertTrue("Expected valid password format for user " + username,
        password.startsWith("SCRAM-SHA-256") || password.startsWith("md5"));
    rs.close();
  }
}
