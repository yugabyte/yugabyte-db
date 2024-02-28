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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestUnixSocketConnections extends BaseYsqlConnMgr {

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);
        builder.addCommonTServerFlag("ysql_enable_auth", "true");
        builder.addCommonTServerFlag("ysql_conn_mgr_password", "wrong pass");
    }

    // TODO(mkumar) GH #19622 Enable the support for restarting Mini Cluster
    // in Ysql Conn Mgr Tests
    @Test
    public void testUnixSocketConnections() throws Exception {
        try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("yugabyte")
                    .withPassword("yugabyte")
                    .connect();)
        {
        } catch (Exception e) {
            LOG.error("Got an unexpected error while creating a connection: ", e);
            fail("connection faced an unexpected issue");
        }
    }
}
