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
import java.util.HashMap;
import java.util.Map;

import org.apache.commons.collections.functors.ExceptionClosure;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestNegUnixSocketConnections extends BaseYsqlConnMgr {

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);
        Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
            {
                put("ysql_hba_conf_csv", "host all all all trust");
                put("ysql_conn_mgr_use_unix_conn", "true");
            }
        };
        // ysql_hba_conf_csv flag would be overwritten with these values from
        // what given in BaseYsqlConnMgr
        builder.addCommonTServerFlags(additionalTserverFlags);
    }

    // TODO(mkumar) GH #19622 Enable the support for restarting Mini Cluster
    // in Ysql Conn Mgr Tests
    @Test
    public void testNegUnixSocketConnections() throws Exception {
        try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("yugabyte")
                    .connect();)
        {
            // This test verifies on enabling unix socket, physical connections won't be
            // authenticated using host in hba.
            fail("Got an unexpected behaviour, ysql conn mgr is able to"  +
                        "create physical connection");
        } catch (Exception e) {
        }
    }

}
