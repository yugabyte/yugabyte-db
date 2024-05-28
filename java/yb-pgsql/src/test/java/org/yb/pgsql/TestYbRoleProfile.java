// Copyright (c) Yugabyte, Inc.
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
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.After;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.YsqlSnapshotVersion;
import org.yb.util.SystemUtil;
import org.yb.YBParameterizedTestRunner;

import com.yugabyte.util.PSQLException;

@RunWith(value = YBParameterizedTestRunner.class)
public class TestYbRoleProfile extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestPgSequences.class);
  private static final String USERNAME = "profile_user";
  private static final String PASSWORD = "profile_password";
  private static final String PROFILE_1_NAME = "prf1";
  private static final String PROFILE_2_NAME = "prf2";
  private static final int PRF_1_FAILED_ATTEMPTS = 3;
  private static final int PRF_2_FAILED_ATTEMPTS = 2;
  private static final Pattern AUTHENTICATION_FAILED_RE = Pattern.compile(
      "FATAL: password authentication failed for user");
  private static final Pattern ROLE_IS_LOCKED_OUT_RE = Pattern.compile(
      "FATAL: role .* is locked. Contact your database administrator.");
  private static final Pattern AUTHENTICATION_REJECTED_RE = Pattern.compile(
      "FATAL: pg_hba.conf rejects connection for host .*, user .*, database \"yugabyte\","
      + " no encryption");
  private static final Pattern NO_HBA_ENTRY_RE = Pattern.compile(
      "FATAL: no pg_hba.conf entry for host .*, user .*, database \"yugabyte\", no encryption");

  private final ConnectionEndpoint connectionEndpoint;

  @Parameterized.Parameters
  public static List<ConnectionEndpoint> parameters() {
    final String enableYsqlConnMgr = System.getenv("YB_ENABLE_YSQL_CONN_MGR_IN_TESTS");
    if (enableYsqlConnMgr != null && enableYsqlConnMgr.equalsIgnoreCase("true"))
      return Arrays.asList(ConnectionEndpoint.YSQL_CONN_MGR);

    if (SystemUtil.IS_LINUX)
      return Arrays.asList(ConnectionEndpoint.POSTGRES, ConnectionEndpoint.YSQL_CONN_MGR);
    else
      return Arrays.asList(ConnectionEndpoint.POSTGRES);
  }

  public TestYbRoleProfile(ConnectionEndpoint connectionEndpoint) {
    this.connectionEndpoint = connectionEndpoint;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    if (connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR)
      builder.enableYsqlConnMgr(true);
      builder.addCommonTServerFlag("ysql_conn_mgr_dowarmup", "false");
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_enable_auth", "true");
    flagMap.put("ysql_enable_profile", "true");
    return flagMap;
  }

  @Override
  protected Connection createTestRole() throws Exception {
    try (Connection initialConnection = getConnectionBuilder().withUser(DEFAULT_PG_USER)
                                                              .withPassword(DEFAULT_PG_PASS)
                                                              .connect();
         Statement statement = initialConnection.createStatement()) {
      statement.execute(
          String.format("CREATE ROLE %s SUPERUSER CREATEROLE CREATEDB BYPASSRLS LOGIN "
                        + "PASSWORD '%s'", TEST_PG_USER, TEST_PG_PASS));
    }

    return getConnectionBuilder().withPassword(TEST_PG_PASS).connect();
  }

  private void attemptLogin(
      String username,
      String password,
      Pattern expectedErrorRe) throws Exception {
    try {
      getConnectionBuilder().withTServer(0)
                            .withUser(username)
                            .withPassword(password)
                            .connect();
      fail("Expected incorrect password");
    } catch (PSQLException e) {
      String msg = e.getMessage();
      Matcher matcher = expectedErrorRe.matcher(msg);
      assertTrue(msg + "does not match regex %s" + expectedErrorRe, matcher.find());
    }
  }

  private void login(String username, String password) throws Exception {
    Connection connection = getConnectionBuilder().withTServer(0)
                                                  .withUser(username)
                                                  .withPassword(password)
                                                  .withConnectionEndpoint(connectionEndpoint)
                                                  .connect();

    connection.close();
  }

  private void assertProfileStateForUser(
      String username,
      int expectedFailedLogins,
      boolean expectedEnabled) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ResultSet result = stmt.executeQuery(
          String.format(
              "SELECT rolprfstatus, rolprffailedloginattempts " +
              "FROM pg_yb_role_profile rp " +
              "JOIN pg_roles rol ON rp.rolprfrole = rol.oid " +
              "WHERE rol.rolname = '%s'",
              username));
      while (result.next()) {
        assertEquals(expectedFailedLogins, Integer.parseInt(
            result.getString("rolprffailedloginattempts")));
        assertEquals(expectedEnabled,
            result.getString("rolprfstatus").equals("o"));
        assertFalse(result.next());
      }
    }
  }

  private String getProfileName(String username) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ResultSet result = stmt.executeQuery(String.format(
          "SELECT prfname " +
              "FROM pg_yb_role_profile rp " +
              "JOIN pg_roles rol ON rp.rolprfrole = rol.oid " +
              "JOIN pg_yb_profile lp ON rp.rolprfprofile = lp.oid " +
              "WHERE rol.rolname = '%s'",
          username));
      while (result.next()) {
        return result.getString("prfname");
      }
    }
    return null;
  }

  private void unlockUserProfile(String username) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER USER %s ACCOUNT UNLOCK", username));
    }
  }

  private void lockUserProfile(String username) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER USER %s ACCOUNT LOCK", username));
    }
  }

  private void detachUserProfile(String username) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER USER %s NOPROFILE", username));
    }
  }

  private void attachUserProfile(String username, String profilename) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER USER %s PROFILE %s", username, profilename));
    }
  }

  private void exceedAttempts(int attemptLimit, String username) throws Exception {
    /* Exceed the failed attempts limit */
    for (int i = 0; i < attemptLimit - 1; i++) {
      attemptLogin(username, "wrong", AUTHENTICATION_FAILED_RE);
    }
    attemptLogin(username, "wrong", ROLE_IS_LOCKED_OUT_RE);
  }

  /** Restart cluster with specified hba conf. */
  private void restartWithHba(String hba) throws Exception {
    Map<String, String> flagMap = super.getTServerFlags();

    // ysql_enable_auth auto-adds an HBA entry, so turn it off.
    flagMap.put("ysql_enable_auth", "false");

    // Add given hba.
    flagMap.put("ysql_hba_conf_csv", hba);
    LOG.info("Restarting with the following HBA config: {}", flagMap.get("ysql_hba_conf_csv"));

    restartClusterWithFlags(Collections.emptyMap(), flagMap);
    setup();
  }

  @After
  public void cleanup() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      /* Cleanup fails if the tables don't exist. */
      boolean profile_exists = stmt.executeQuery("SELECT 1 FROM pg_class WHERE" +
          " relname = 'pg_yb_profile'").next();

      if (profile_exists) {
        if (getProfileName(USERNAME) != null) {
          stmt.execute(String.format("ALTER USER %s NOPROFILE", USERNAME));
        }
        stmt.execute(String.format("DROP PROFILE IF EXISTS %s", PROFILE_1_NAME));
        stmt.execute(String.format("DROP PROFILE IF EXISTS %s", PROFILE_2_NAME));
        stmt.execute(String.format("DROP USER IF EXISTS %s", USERNAME));
      }
    }
  }

  @Before
  public void setup() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE USER %s PASSWORD '%s'", USERNAME, PASSWORD));
      stmt.execute(String.format("CREATE PROFILE %s LIMIT FAILED_LOGIN_ATTEMPTS %d",
                                 PROFILE_1_NAME, PRF_1_FAILED_ATTEMPTS));
      stmt.execute(String.format("CREATE PROFILE %s LIMIT FAILED_LOGIN_ATTEMPTS %d",
                                 PROFILE_2_NAME, PRF_2_FAILED_ATTEMPTS));
      stmt.execute(String.format("ALTER USER %s PROFILE %s",
                                 USERNAME, PROFILE_1_NAME));
    }
  }

  @Test
  @Ignore // YB_TODO: Enable test when recreateWithYsqlVersion is working.
  public void testProfilesOnOldDbVersion() throws Exception {
    Assume.assumeFalse("Skipping this test for Ysql Connection Manager",
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR);

    /*
    * There are two operations that might result in an error if the profile catalogs don't exist:
    *  1. logging in, because auth.c tries to get the user's profile if it exists
    *  2. running profile commands.
    * We can simply test this by logging in (which should behave as normal) and running a command.
    */
    recreateWithYsqlVersion(YsqlSnapshotVersion.EARLIEST);

    try (Connection conn = getConnectionBuilder().withDatabase("template1")
                                                 .withTServer(0)
                                                 .withUser(DEFAULT_PG_USER)
                                                 .withPassword(DEFAULT_PG_PASS)
                                                 .connect();
         Statement stmt = conn.createStatement()) {
      runInvalidQuery(stmt,
                      "CREATE PROFILE p LIMIT FAILED_LOGIN_ATTEMPTS 3",
                      "Login profile system catalogs do not exist");
    }
  }

  @Test
  public void testAdminCanChangeUserProfile() throws Exception {
    Assume.assumeFalse("Skipping this test for Ysql Connection Manager",
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR);

    assertEquals(PROFILE_1_NAME, getProfileName(USERNAME));

    exceedAttempts(PRF_1_FAILED_ATTEMPTS, USERNAME);
    assertProfileStateForUser(USERNAME, PRF_1_FAILED_ATTEMPTS, false);

    /* When the profile is removed, the user can log in again */
    detachUserProfile(USERNAME);
    login(USERNAME, PASSWORD);

    attachUserProfile(USERNAME, PROFILE_1_NAME);
    unlockUserProfile(USERNAME);
    assertEquals(PROFILE_1_NAME, getProfileName(USERNAME));
    login(USERNAME, PASSWORD);

    /* Then, if we change the profile, the user has only that many failed attempts */
    attachUserProfile(USERNAME, PROFILE_2_NAME);
    assertEquals(PROFILE_2_NAME, getProfileName(USERNAME));

    exceedAttempts(PRF_2_FAILED_ATTEMPTS, USERNAME);
    assertProfileStateForUser(USERNAME, PRF_2_FAILED_ATTEMPTS, false);
  }

  @Test
  public void testRegularUserCanFailLoginManyTimes() throws Exception {
    Assume.assumeFalse("Skipping this test for Ysql Connection Manager",
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR);

    for (int i = 0; i < 10; i++) {
      attemptLogin(TEST_PG_USER, "wrong", AUTHENTICATION_FAILED_RE);
    }
    login(TEST_PG_USER, TEST_PG_PASS);
  }

  @Test
  public void testAdminCanUnlockAfterLockout() throws Exception {
    Assume.assumeFalse("Skipping this test for Ysql Connection Manager",
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR);

    exceedAttempts(PRF_1_FAILED_ATTEMPTS, USERNAME);
    assertProfileStateForUser(USERNAME, PRF_1_FAILED_ATTEMPTS, false);

    /* Now the user cannot login */
    attemptLogin(USERNAME, PASSWORD, ROLE_IS_LOCKED_OUT_RE);

    /* After an admin resets, the user can login again */
    unlockUserProfile(USERNAME);
    assertProfileStateForUser(USERNAME, 0, true);
    login(USERNAME, PASSWORD);
  }

  @Test
  public void testAdminCanLockAndUnlock() throws Exception {
    Assume.assumeFalse("Skipping this test for Ysql Connection Manager",
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR);

    /* Make one failed attempt. */
    attemptLogin(USERNAME, "wrong", AUTHENTICATION_FAILED_RE);
    assertProfileStateForUser(USERNAME, 1, true);

    lockUserProfile(USERNAME);

    /* With a locked profile, the user cannot login. The count should not increment. */
    assertProfileStateForUser(USERNAME, 1, false);
    attemptLogin(USERNAME, PASSWORD, ROLE_IS_LOCKED_OUT_RE);
    assertProfileStateForUser(USERNAME, 1, false);

    /* After an admin unlocks, the user can login again */
    unlockUserProfile(USERNAME);
    assertProfileStateForUser(USERNAME, 0, true);
    login(USERNAME, PASSWORD);
  }

  @Test
  public void testLogin() throws Exception {
    /* The initial state allows logins */
    assertProfileStateForUser(USERNAME, 0, true);
    login(USERNAME, PASSWORD);

    /* Use up all allowed failed attempts */
    for (int i = 0; i < PRF_1_FAILED_ATTEMPTS - 1; i++) {
      attemptLogin(USERNAME, "wrong", AUTHENTICATION_FAILED_RE);
      assertProfileStateForUser(USERNAME, i + 1, true);
    }

    /* A successful login wipes the slate clean */
    login(USERNAME, PASSWORD);
    assertProfileStateForUser(USERNAME, 0, true);

    exceedAttempts(PRF_1_FAILED_ATTEMPTS, USERNAME);
    assertProfileStateForUser(USERNAME, PRF_1_FAILED_ATTEMPTS, false);

    /*
     * Now even the correct password will not let us in.
     * Failed attempts above the limit + 1 are not counted.
     */
    attemptLogin(USERNAME, PASSWORD, ROLE_IS_LOCKED_OUT_RE);
    attemptLogin(USERNAME, "wrong", ROLE_IS_LOCKED_OUT_RE);
    assertProfileStateForUser(USERNAME, PRF_1_FAILED_ATTEMPTS, false);
  }

  @Test
  public void testRejectLogin() throws Exception {
    restartWithHba(String.format("\"host all %s all reject\",\"host all all all md5\"", USERNAME));

    // Initial state is rolprffailedloginattempts = 0.
    assertProfileStateForUser(USERNAME, 0, true);

    // Login with correct password fails as auth method is reject.
    attemptLogin(USERNAME, PASSWORD, AUTHENTICATION_REJECTED_RE);
    // Even though login failed, rolprffailedloginattempts does not change.
    assertProfileStateForUser(USERNAME, 0, true);

    // Restart the cluster with default flags.
    restartClusterWithFlags(Collections.emptyMap(), Collections.emptyMap());
  }

  @Test
  public void testImplicitRejectLogin() throws Exception {
    restartWithHba(String.format(
          "\"host all %s all trust\",\"host all %s all trust\",\"host all %s all trust\"",
          TEST_PG_USER, "postgres", "yugabyte"));

    // Initial state is rolprffailedloginattempts = 0.
    assertProfileStateForUser(USERNAME, 0, true);

    // Login with correct password fails as auth method is implicit reject.
    attemptLogin(USERNAME, PASSWORD, NO_HBA_ENTRY_RE);
    // Even though login failed, rolprffailedloginattempts does not change.
    assertProfileStateForUser(USERNAME, 0, true);

    // Restart the cluster with default flags.
    restartClusterWithFlags(Collections.emptyMap(), Collections.emptyMap());
  }

  @Test
  public void testTrustLogin() throws Exception {
    restartWithHba(String.format("\"host all %s all trust\",\"host all all all md5\"", USERNAME));

    lockUserProfile(USERNAME);
    assertProfileStateForUser(USERNAME, 0, false);

    // Login with any password as auth method is trust
    login(USERNAME, "wrong");
    // Even though login is successful, account remains locked.
    assertProfileStateForUser(USERNAME, 0, false);

    // Restart the cluster with default flags.
    restartClusterWithFlags(Collections.emptyMap(), Collections.emptyMap());
  }

  @Test
  public void testYbTserverKeyLogin() throws Exception {
    final String backfillProfile = "backfill_profile";
    final String backfillUser = "postgres";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE PROFILE %s LIMIT FAILED_LOGIN_ATTEMPTS %d",
                                 backfillProfile, 3));
      stmt.execute(String.format("ALTER USER %s PROFILE %s", backfillUser, backfillProfile));
    }

    attemptLogin(backfillUser, "wrong", AUTHENTICATION_FAILED_RE);
    lockUserProfile(backfillUser);
    assertProfileStateForUser(backfillUser, 1, false);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE tab (i int)");
      stmt.execute("CREATE INDEX ON tab (i)");
    }
    // Even though login via index backfill is successful, account status is not changed.
    assertProfileStateForUser(backfillUser, 1, false);
  }
}
