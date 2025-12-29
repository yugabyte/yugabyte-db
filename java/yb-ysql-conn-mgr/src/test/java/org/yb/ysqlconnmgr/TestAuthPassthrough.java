package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import com.google.gson.JsonObject;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestAuthPassthrough extends BaseYsqlConnMgr {
  final String TEST_USERNAME = "user1";
  final String TEST_PASSWORD = "pwd";
  final String ADMIN_USERNAME = "yugabyte";
  final String ADMIN_PASSWORD = "yugabyte";
  final String WRONG_PASSWORD = "abcd"; // Non-empty wrong password

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("enable_ysql_conn_mgr", "true");
        put("ysql_conn_mgr_use_auth_backend", "false");
        put("ysql_conn_mgr_superuser_sticky", "false");
        put("ysql_enable_auth", "true");
        put("ysql_conn_mgr_log_settings", "log_debug,log_query");
        put("allowed_preview_flags_csv", "ysql_conn_mgr_version_matching");
        put("ysql_conn_mgr_version_matching", "true");
      }
    };

    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  @Override
  public ConnectionBuilder connectionBuilderForVerification(ConnectionBuilder builder) {
    return builder.withUser("yugabyte").withPassword("yugabyte");
  }

  @Before
  public void SetupTestUser() throws Exception {
    try (Connection connection = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
             .withUser(ADMIN_USERNAME)
             .withPassword(ADMIN_PASSWORD)
             .connect();
        Statement statement = connection.createStatement()) {
      statement.execute("CREATE USER " + TEST_USERNAME + " WITH PASSWORD '" + TEST_PASSWORD + "'");
    }
  }

  @After
  public void cleanUpAfter() throws Exception {
    LOG.info("Cleaning up after {}", getCurrentTestMethodName());
    markClusterNeedsRecreation();
  }

  @Test
  public void testConsecutiveConnections() throws Exception {
    // Connect 5 times in a row
    for (int iteration = 0; iteration < 5; iteration++) {
      try (Connection connection = getConnectionBuilder()
               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
               .withUser(TEST_USERNAME)
               .withPassword(TEST_PASSWORD)
               .connect();
          Statement statement = connection.createStatement()) {
        statement.executeQuery("SELECT 1");
      }
    }

    Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);
    JsonObject pool = getPool("control_connection", "control_connection");
    assertEquals("Number of physical connections should be 1 after authentication is over.", 1,
        pool.get("idle_physical_connections").getAsInt());
  }

  // Try logging in after a failed attempt. The control backend *should* have aborted the failed
  // attempt. Specifically, it should not go on to process the startup packet GUC opts and send bacl
  // 'r' ParameterStatus packets. It should be ready for the next auth request.
  @Test
  public void testConnectionFailure() throws Exception {
    try (Connection connection = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(TEST_USERNAME)
             .withPassword(WRONG_PASSWORD)
             .connect();
        Statement statement = connection.createStatement()) {
      fail("Wrong password login should have failed");
    } catch (SQLException e) {
      LOG.info("Expected login failure. Got error message: ", e);
    }

    Thread.sleep(2*STATS_UPDATE_INTERVAL * 1_000);

    JsonObject pool = getPool("control_connection", "control_connection");

    int num_physical_conn = pool.get("active_physical_connections").getAsInt()
        + pool.get("idle_physical_connections").getAsInt();
    assertEquals("Control backend should not have closed after failed auth ", num_physical_conn, 1);

    try (Connection connection = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(TEST_USERNAME)
             .withPassword(TEST_PASSWORD)
             .connect();
        Statement statement = connection.createStatement()) {
      statement.executeQuery("SELECT 1");
    }
  }

  // Helper functions to get & set GUC values or defaults. Exceptions are propagated to the caller.
  private void SetGUC(String user, String password, String gucName, String gucValue,
      Boolean setDefault) throws Exception {
    try (Connection connection = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(user)
             .withPassword(password)
             .connect();
        Statement statement = connection.createStatement()) {
      String alter = "";
      if (setDefault) {
        alter = "ALTER ROLE " + user + " ";
      }
      statement.execute(alter + "SET " + gucName + " = '" + gucValue + "'");
    }
  }

  private String GetGUC(String user, String password, String gucName) throws Exception {
    try (Connection connection = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(user)
             .withPassword(password)
             .connect();
        Statement statement = connection.createStatement()) {
      ResultSet res = statement.executeQuery("SHOW " + gucName);
      if (res.next()) {
        return res.getString(1);
      }
    }

    return "";
  }

  private String GetGUCFromConnection(Connection conn, String gucName) throws Exception {
    try (Statement statement = conn.createStatement()) {
      ResultSet res = statement.executeQuery("SHOW " + gucName);
      if (res.next()) {
        return res.getString(1);
      }
    }

    return "";
  }

  // Test that GUC state does not bleed between txn and control backends and that defaults get
  // applied correctly.
  // This test deals with the GUC sources PGS_S_SESSION and PGC_S_[GLOBAL/DATABASE/USER] only and
  // explicitly does not make use of the startup packet (which would be PGC_S_CLIENT) for setting
  // GUCs.
  @Test
  public void testGUCResetAndDefaults() throws Exception {
    {
      // Assert that new connections are not polluted with GUC assigments from existing connections.
      SetGUC(TEST_USERNAME, TEST_PASSWORD, "debug_pretty_print", "off", false);
      String gucValue = GetGUC(TEST_USERNAME, TEST_PASSWORD, "debug_pretty_print");
      assertEquals("GUC value should not be overridden for a new connection", "on", gucValue);
    }
    {
      // Assert that new connections get the correct defaults
      SetGUC(TEST_USERNAME, TEST_PASSWORD, "debug_pretty_print", "off", true);
      String gucValue = GetGUC(TEST_USERNAME, TEST_PASSWORD, "debug_pretty_print");
      assertEquals("New connection should get updated GUC defaults", "off", gucValue);
    }
    {
      // Assert that other users aren't affected by an ALTER ROLE command for a given user.
      String gucValue = GetGUC(ADMIN_USERNAME, ADMIN_PASSWORD, "debug_pretty_print");
      assertEquals(
          "GUC defaults should not bleed across users for user-specific defaults", "on", gucValue);
    }
    {
      // Assert that a logged in non-superuser cannot set superuser-only GUCs.
      try {
        SetGUC(TEST_USERNAME, TEST_PASSWORD, "track_counts", "off", false);
        fail("Non-superuser should not be able to set PGC_SUSET context GUCs");
      } catch (SQLException e) {
        LOG.info("Expected failure to set GUC. Got exception: ", e);
      }
    }
  }

  // Test that GUC vars specified in the startup packet are applied correctly (with appropriate
  // privilege checks) and that they do not bleed across authentication attempts.
  @Test
  public void testGUCOptsInStartupPacket() throws Exception {
    Properties props = new Properties();
    props.put("options", "-c debug_pretty_print=off");
    try (Connection connection = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(TEST_USERNAME)
             .withPassword(TEST_PASSWORD)
             .connect(props)) {
      String gucValue = GetGUCFromConnection(connection, "debug_pretty_print");
      assertEquals("GUC values should be applied from startup packet", "off", gucValue);
    }

    try (Connection connection = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(TEST_USERNAME)
             .withPassword(TEST_PASSWORD)
             .connect()) {
      String gucValue = GetGUCFromConnection(connection, "debug_pretty_print");
      assertEquals(
          "GUC values applied from startup packet should not bleed across clients", "on", gucValue);
    }

    Properties props_su = new Properties();
    props_su.put("options", "-c track_counts=off");
    try (Connection connection = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(TEST_USERNAME)
             .withPassword(TEST_PASSWORD)
             .connect(props_su)) {
      fail("Should not be able to log in while setting PGC_SUSET GUC var from non superuser.");
    } catch (SQLException e) {
      LOG.info(
          "Expected failure when setting PGC_SUSET GUC var in startup packet. Got exception: ", e);
    }
  }
}
