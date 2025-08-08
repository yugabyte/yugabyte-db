package org.yb.ysqlconnmgr;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestAuthPassthrough extends BaseYsqlConnMgr {
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
      }
    };

    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  @Test
  public void testConsecutiveConnections() throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.DEFAULT)
            .withUser("yugabyte")
            .withPassword("yugabyte")
            .connect();
         Statement statement = connection.createStatement()) {
        // Create user1
        statement.execute("CREATE USER user1 WITH PASSWORD 'password1'");
    }

    // Connect 5 times in a row
    for (int iteration = 0; iteration < 5; iteration++) {
        try (Connection connection = getConnectionBuilder()
              .withConnectionEndpoint(ConnectionEndpoint.DEFAULT)
              .withUser("user1")
              .withPassword("password1")
              .connect();
           Statement statement = connection.createStatement()) {
            statement.executeQuery("SELECT 1");
        }
    }
  }
}
