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
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestCorrectErrorMessage extends BaseYsqlConnMgr {
  private static final String CUSTOM_PG_HBA_CONFIG =
      "host    yugabyte    yugabyte   all   md5, " +
      "host    wrong_db    yugabyte    all    trust, " +
      "host    all    yugabyte     all    md5, " +
      "host    all    wrong_user   all   trust, " +
      "host    all    all          all    trust, " +
      "local   all    all   trust";

  private static final String WRONG_USER_ERR_STRING = "role \"wrong_user\" does not exist";
  private static final String WRONG_DB_ERR_STRING = "database \"wrong_db\" does not exist";

  @Override
  protected String getHbaConf() {
    return CUSTOM_PG_HBA_CONFIG;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    // At max there should be only one connection in the control connection pool.
    // pool size of control connection = ysql_max_connections / 10
    builder.addCommonTServerFlag("ysql_max_connections", "15");
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
      /* Match error message */
      assertTrue("Wrong error message", e.getMessage().contains(WRONG_DB_ERR_STRING));
    }

    checkBrokenControlConnection();
  }
}
