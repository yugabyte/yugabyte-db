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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

/**
 * Tests for SCRAM-SHA-256 as the default password encryption method.
 * This test validates YugabyteDB's change from MD5 to SCRAM-SHA-256 as the default.
 */
@RunWith(value = YBTestRunner.class)
public class TestSCRAMDefault extends BasePgSQLTest {

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
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {

      String scramUser = "test_scram_auth_user";
      String md5User = "test_md5_auth_user";

      try {
        stmt.execute("DROP ROLE IF EXISTS " + scramUser);
        stmt.execute("DROP ROLE IF EXISTS " + md5User);

        // Test 4: Create SCRAM user (default behavior)
        stmt.execute("CREATE ROLE " + scramUser + " LOGIN PASSWORD 'scrampass123'");

        // Test 5: Create MD5 user (explicit setting)
        stmt.execute("SET password_encryption = 'md5'");
        stmt.execute("CREATE ROLE " + md5User + " LOGIN PASSWORD 'md5pass123'");
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

        // Test 6: Both users should be able to authenticate
        // We can't easily test actual authentication from within the same connection,
        // but we can verify the password entries are properly formatted
        verifyPasswordIsValid(stmt, scramUser);
        verifyPasswordIsValid(stmt, md5User);

      } finally {
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
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {

      String[] testUsers = {"mixed_test_user1", "mixed_test_user2",
          "mixed_test_user3"};

      try {
        for (String user : testUsers) {
          stmt.execute("DROP ROLE IF EXISTS " + user);
        }

        // Test 10: Create users with different password encryption settings
        // User 1: Default (should be SCRAM)
        stmt.execute("CREATE ROLE " + testUsers[0] + " LOGIN PASSWORD 'pass1'");

        // User 2: Explicit MD5
        stmt.execute("SET password_encryption = 'md5'");
        stmt.execute("CREATE ROLE " + testUsers[1] + " LOGIN PASSWORD 'pass2'");

        // User 3: Back to SCRAM
        stmt.execute("SET password_encryption = 'scram-sha-256'");
        stmt.execute("CREATE ROLE " + testUsers[2] + " LOGIN PASSWORD 'pass3'");

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

      } finally {
        for (String user : testUsers) {
          stmt.execute("DROP ROLE IF EXISTS " + user);
        }
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
