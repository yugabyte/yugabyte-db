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

import static org.yb.AssertionWrappers.assertNotNull;

import java.sql.ResultSet;
import java.sql.Statement;
import java.io.IOException;
import java.net.URL;
import java.util.HashMap;
import java.util.Map;
import java.util.Scanner;
import org.junit.AfterClass;
import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.TestUtils;
import org.yb.minicluster.*;
import org.yb.pgsql.BasePgSQLTest;
import org.yb.pgsql.ConnectionBuilder;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

public class BaseYsqlConnMgr extends BaseMiniClusterTest {
  protected static final Logger LOG = LoggerFactory.getLogger(BaseYsqlConnMgr.class);
  protected static final int NUM_TSERVER = 3;
  private static final String DEFAULT_PG_USER = "yugabyte";
  protected static final int STATS_UPDATE_INTERVAL = 2;
  private boolean warmup_random_mode = true;

  protected static final String DISABLE_TEST_WITH_ASAN =
        "Test is not working correctly with asan build";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enableYsql(true);
    builder.enableYsqlConnMgr(true);
    builder.numTservers(NUM_TSERVER);
    builder.replicationFactor(NUM_TSERVER);
    builder.addCommonTServerFlag("ysql_conn_mgr_dowarmup", "false");
    if (warmup_random_mode) {
      builder.addCommonTServerFlag(
      "TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "random");
    }
  }

  protected ConnectionBuilder getConnectionBuilder() {
    return new ConnectionBuilder(miniCluster).withUser(DEFAULT_PG_USER);
  }

  protected void disableWarmupRandomMode(MiniYBClusterBuilder builder) {
    builder.addCommonTServerFlag(
        "TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    warmup_random_mode = false;
    return;
  }

  public String getPgHost(int tserverIndex) {
    return miniCluster.getPostgresContactPoints().get(tserverIndex).getHostName();
  }

  protected boolean isTestRunningInWarmupRandomMode() {
    return warmup_random_mode;
  }

  @Before
  public void waitForDatabaseToStart() throws Exception {
    LOG.info("Waiting for initdb to complete on master");
    TestUtils.waitFor(
        () -> {
          IsInitDbDoneResponse initdbStatusResp = miniCluster.getClient().getIsInitDbDone();
          if (initdbStatusResp.hasError()) {
            throw new RuntimeException(
                "Could not request initdb status: " + initdbStatusResp.getServerError());
          }
          String initdbError = initdbStatusResp.getInitDbError();
          if (initdbError != null && !initdbError.isEmpty()) {
            throw new RuntimeException("initdb failed: " + initdbError);
          }
          return initdbStatusResp.isDone();
        },
        600000);
    LOG.info("initdb has completed successfully on master");
  }

  @AfterClass
  public static void waitForProperShutdown() throws InterruptedException {
    // Wait for 1 sec before stoping the miniCluster so that Ysql Connection Manger can clean the
    // shared memory.
    Thread.sleep(1000);
  }

  boolean verifySessionParameterValue(Statement stmt, String param, String expectedValue)
      throws Exception {
    String query = String.format("SHOW %s", param);
    LOG.info(String.format("Executing query `%s`", query));

    ResultSet resultSet = stmt.executeQuery(query);
    assertNotNull(resultSet);

    if (!resultSet.next()) {
      LOG.error("Got empty result for SHOW query");
      return false;
    }

    if (!resultSet.getString(1).toLowerCase().equals(expectedValue.toLowerCase())) {
      LOG.error("Expected value " + expectedValue + " is not same as the query result "
          + resultSet.getString(1));
      return false;
    }

    return true;
  }

  protected abstract class TestConcurrently implements Runnable {
    protected String dbName;
    public Boolean testSuccess;

    public TestConcurrently(String dbName) {
      this.dbName = dbName;
      this.testSuccess = false;
    }
  }
}
