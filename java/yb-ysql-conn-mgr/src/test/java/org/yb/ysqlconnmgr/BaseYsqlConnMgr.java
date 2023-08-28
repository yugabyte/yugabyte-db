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

import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.TestUtils;
import org.yb.minicluster.*;
import org.yb.pgsql.ConnectionBuilder;

public class BaseYsqlConnMgr extends BaseMiniClusterTest {
  protected static final Logger LOG = LoggerFactory.getLogger(BaseYsqlConnMgr.class);
  protected static final int NUM_TSERVER = 3;
  private static final String DEFAULT_PG_USER = "yugabyte";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enableYsql(true);
    builder.enableYsqlConnMgr(true);
    builder.numTservers(NUM_TSERVER);
    builder.replicationFactor(NUM_TSERVER);
    builder.addCommonTServerFlag("ysql_conn_mgr_dowarmup", "false");
  }

  protected ConnectionBuilder getConnectionBuilder() {
    return new ConnectionBuilder(miniCluster).withUser(DEFAULT_PG_USER);
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
}
