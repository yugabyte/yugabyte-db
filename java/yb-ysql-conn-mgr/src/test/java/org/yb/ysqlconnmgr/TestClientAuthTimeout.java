package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.TimeUnit;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.RequiresLinux;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestClientAuthTimeout extends BaseYsqlConnMgr {
  private final int AUTH_MSG_TIMEOUT = 2000; // Shortened to make test faster
  private final int socketTimeout = 20;
  private final int driverSleepTime_ms = AUTH_MSG_TIMEOUT + 500;
  private final String USER = "yugabyte";
  private final String PASS = "yugabyte";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_enable_auth", "true");
        put("enable_ysql_conn_mgr", "true");
        put("ysql_conn_mgr_auth_msg_timeout", String.valueOf(AUTH_MSG_TIMEOUT));
      }
    };

    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  @Override
  public ConnectionBuilder connectionBuilderForVerification(ConnectionBuilder builder) {
    return builder.withUser(USER).withPassword(PASS);
  }

  @Test
  public void testClientAuthTimeoutAB() throws Exception {
    restartClusterWithAdditionalFlags(java.util.Collections.emptyMap(),
        java.util.Collections.singletonMap("ysql_conn_mgr_use_auth_backend", "false"));

    Properties props = new Properties();
    props.setProperty("socketTimeout", String.valueOf(socketTimeout));
    props.setProperty("socketFactory", "org.yb.ysqlconnmgr.CloseAfterAuthRequestSocketFactory");
    props.setProperty("socketFactoryDelayMs", String.valueOf(driverSleepTime_ms));
    props.setProperty("socketFactoryCloseAfterAuth", "false");
    ConnectionBuilder builder = getConnectionBuilder();
    int max_attempts = builder.getMaxConnectionAttempts();
    builder.setMaxConnectionAttempts(1);
    try (Connection connection = builder.withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(USER)
             .withPassword(PASS)
             .connect(props)) {
      fail("Should not have been able to log in due to conn mgr auth timeout.");
    } catch (SQLException e) {
      LOG.info("Expected failure: " + e.getMessage());
    } catch (Exception e) {
      fail("Unexpected Exception during authentication: " + e);
    } finally {
      builder.setMaxConnectionAttempts(max_attempts);
    }
  }

  @Test
  public void testClientAuthTimeoutAP() throws Exception {
    restartClusterWithAdditionalFlags(java.util.Collections.emptyMap(),
        java.util.Collections.singletonMap("ysql_conn_mgr_use_auth_backend", "false"));

    Properties props = new Properties();
    props.setProperty("socketTimeout", String.valueOf(socketTimeout));
    props.setProperty("socketFactory", "org.yb.ysqlconnmgr.CloseAfterAuthRequestSocketFactory");
    props.setProperty("socketFactoryDelayMs", String.valueOf(driverSleepTime_ms));
    props.setProperty("socketFactoryCloseAfterAuth", "false");
    ConnectionBuilder builder = getConnectionBuilder();
    int max_attempts = builder.getMaxConnectionAttempts();
    builder.setMaxConnectionAttempts(1);
    try (Connection connection = builder.withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser(USER)
             .withPassword(PASS)
             .connect(props)) {
      fail("Should not have been able to log in due to conn mgr auth timeout.");
    } catch (SQLException e) {
      LOG.info("Expected failure: " + e.getMessage());
    } catch (Exception e) {
      fail("Unexpected Exception during authentication: " + e);
    } finally {
      builder.setMaxConnectionAttempts(max_attempts);
    }
  }

  // Logs in with a client that waits 2.5 s before answering the password request. That fails
  // with the 2 s timeout. After ysql_conn_mgr_auth_msg_timeout is raised to 10 s at runtime,
  // the same login succeeds. After it is lowered back to 2 s, it fails again.
  @Test
  public void testClientAuthTimeoutRuntimeChange() throws Exception {
    assertLoginFails();

    setAuthMsgTimeout(10000);
    try (Connection connection = delayedLoginBuilder().connect(delayedLoginProps())) {
      LOG.info("Logged in after raising ysql_conn_mgr_auth_msg_timeout");
    }

    setAuthMsgTimeout(AUTH_MSG_TIMEOUT);
    assertLoginFails();
  }

  private void setAuthMsgTimeout(int timeoutMs) throws Exception {
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, 0);
    setServerFlagOnAll("ysql_conn_mgr_auth_msg_timeout", String.valueOf(timeoutMs));
    assertNotNull("Expected odyssey to reload its config",
        tailer.waitForLogRegex("reconfigure rules", 10, TimeUnit.SECONDS));
  }

  private Properties delayedLoginProps() {
    Properties props = new Properties();
    props.setProperty("socketTimeout", String.valueOf(socketTimeout));
    props.setProperty("socketFactory", "org.yb.ysqlconnmgr.CloseAfterAuthRequestSocketFactory");
    props.setProperty("socketFactoryDelayMs", String.valueOf(driverSleepTime_ms));
    props.setProperty("socketFactoryCloseAfterAuth", "false");
    return props;
  }

  private ConnectionBuilder delayedLoginBuilder() {
    return getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser(USER)
        .withPassword(PASS);
  }

  private void assertLoginFails() throws Exception {
    ConnectionBuilder builder = delayedLoginBuilder();
    builder.setMaxConnectionAttempts(1);
    try (Connection connection = builder.connect(delayedLoginProps())) {
      fail("Should not have been able to log in due to conn mgr auth timeout.");
    } catch (SQLException e) {
      LOG.info("Expected failure: " + e.getMessage());
    }
  }
}
