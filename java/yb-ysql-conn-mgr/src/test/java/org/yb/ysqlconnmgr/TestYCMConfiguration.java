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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestYCMConfiguration extends BaseYsqlConnMgr {

  private static final String LONG_RAND_STR =
      "randonmlongstringabcdefgergekrjbgferkjbferjkberkjghbverjkh";

  @Test
  public void testQuerySizeGflag() throws Exception {

    try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .connect();
          Statement stmt = conn.createStatement()) {

          stmt.execute("SET application_name to " + LONG_RAND_STR);
          ResultSet rs = stmt.executeQuery("show application_name");

          if (rs.next())
            assertEquals(LONG_RAND_STR, rs.getString(1));
    }
    catch (Exception e) {
      LOG.error("Got an unexpected error: ", e);
      fail("Connection faced an unexpected issue");
    }

    // Decrease the size of query packet and restart the cluster.
    // The new query size '100' has been assigned by considering the length
    // of deploy phase query for master and 2024.2 branch. As there is slight
    // difference in the implementation of reportGUCOption() function in guc.c
    // file for both branches.
    reduceQuerySizePacketAndRestartCluster(100);

    try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .connect();
          Statement stmt = conn.createStatement()) {

          stmt.execute("SET application_name to " + LONG_RAND_STR);
          ResultSet rs = stmt.executeQuery("show application_name");

          if (rs.next()) {
            // All the GUC variables which are implicitly set by JDBC and
            // application_name set explicitly to large random value can not
            // fit into a query array of size 128 therefore it won't lead to
            // correct results and none of the GUC variable will be set with
            // connection manager.
            assertEquals("", rs.getString(1));
          }
    }
    catch (Exception e) {
      LOG.error("Got an unexpected error: ", e);
      fail("Connection faced an unexpected issue");
    }

  }

}
