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

package org.yb.ysqlconnmgr;

import java.sql.Connection;
import java.sql.Statement;
import java.util.Collections;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.RequiresLinux;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestCorrectErrorMessage extends BaseYsqlConnMgr {
  private static final String CUSTOM_PG_HBA_CONFIG =
      "host    yugabyte    yugabyte   all   md5, " +
      "host    wrong_db    yugabyte    all    trust, " +
      "host    all    yugabyte     all    md5, " +
      "host    all    wrong_user   all   trust, " +
      "host    all    all          all    trust";

  private static final String WRONG_USER_ERR_STRING = "role \"wrong_user\" does not exist";
  private static final String WRONG_DB_ERR_STRING = "database \"wrong_db\" does not exist";


  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    // At max there should be only one connection in the control connection pool.
    // pool size of control connection = ysql_max_connections / 10
    builder.addCommonTServerFlag("ysql_max_connections", "15");
    builder.addCommonTServerFlag("ysql_hba_conf_csv", CUSTOM_PG_HBA_CONFIG);
  }

  @Override
  public ConnectionBuilder connectionBuilderForVerification(ConnectionBuilder builder) {
    return builder.withUser("yugabyte").withPassword("yugabyte");
  }

  public void checkBrokenControlConnection() throws Exception {
    try (Connection connection =
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                              .withUser("yugabyte")
                              .withPassword("yugabyte")
                              .connect()) {
    };
  }

  // GH #19781: If the authentication fails, the control connection gets broken and next time when
  // any client attempts making connection
  // "broken server connection" error message is received.
  // This test ensures that client can retry making connection without getting "broken server
  // connection" error message.
  @Test
  public void testNoBrokenConnAfterAuthFailed() throws Exception {
    try {
      getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                            .withDatabase("yugabyte")
                            .withUser("yugabyte")
                            .withPassword("wrong password")
                            .connect();
      fail("Did not expected to login with a wrong password");
    } catch (Exception e) {
      LOG.info("Got an expected error message", e);
    }

    checkBrokenControlConnection();
  }

  @Test
  public void testWrongUserErrMsg() throws Exception {
    try (Connection connection =
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                              .withUser("wrong_user")
                              .withPassword("yugabyte")
                              .connect()) {
      fail("Did not expected to login with a wrong username");
    } catch (Exception e) {
      assertTrue("Got wrong error message", e.getMessage().contains(WRONG_USER_ERR_STRING));
    }

    checkBrokenControlConnection();
  }

  @Test
  public void testWrongDbErrMsg() throws Exception {
    try (Connection connection =
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                              .withUser("yugabyte")
                              .withDatabase("wrong_db")
                              .connect()) {
      fail("Did not expected to login with a wrong db");
    } catch (Exception e) {
      // Match error message
      assertTrue("Wrong error message", e.getMessage().contains(WRONG_DB_ERR_STRING));
    }

    checkBrokenControlConnection();
  }

  // Logs in as a role that does not exist, with %08x and %% in its name.
  // The FATAL message shows the name as it was sent.
  @Test
  public void testFormatCharsInLoginErrMsg() throws Exception {
    final String user = "no_user_%08x.%08x.%08x.%08x_%%";
    try (Connection connection =
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                              .withUser(user)
                              .withPassword("yugabyte")
                              .connect()) {
      fail("Did not expect to login with a wrong username");
    } catch (Exception e) {
      assertTrue("Got wrong error message: " + e.getMessage(),
                 e.getMessage().contains("role \"" + user + "\" does not exist"));
    }

    checkBrokenControlConnection();
  }

  // Sets default_text_search_config to a config with %08x in its name, then drops the
  // config from a direct connection. The next query runs on another backend, where
  // replaying the SET fails. The FATAL message shows the config name as it was set.
  @Test
  public void testFormatCharsInDeployErrMsg() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithAdditionalFlags(Collections.emptyMap(),
        Collections.singletonMap("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "round_robin"));

    final String cfg = "public.\"cfg_%08x.%08x\"";
    try (Connection conn =
             getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                   .withUser("yugabyte")
                                   .withPassword("yugabyte")
                                   .connect();
         Connection directConn =
             getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
                                   .withUser("yugabyte")
                                   .withPassword("yugabyte")
                                   .connect();
         Statement stmt = conn.createStatement();
         Statement directStmt = directConn.createStatement()) {
      directStmt.execute("CREATE TEXT SEARCH CONFIGURATION " + cfg + " (COPY = english)");
      stmt.execute("SET default_text_search_config = '" + cfg + "'");
      directStmt.execute("DROP TEXT SEARCH CONFIGURATION " + cfg);
      try {
        stmt.execute("SELECT 1");
        fail("Expected the SET replay on another backend to fail");
      } catch (Exception e) {
        assertTrue("Got wrong error message: " + e.getMessage(),
                   e.getMessage().contains(
                       "invalid value for parameter \"default_text_search_config\": \"" +
                       cfg + "\""));
      }
    }
  }
}
