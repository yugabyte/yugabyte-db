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
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

/**
 * Regression test for the connection manager's handling of YB-internal
 * ParameterStatus packets that are larger than Odyssey's per-connection
 * readahead buffer.
 *
 * When a GUC changes on a conn-mgr-backed session, the backend emits a
 * special ParameterStatus packet (type 'r', {@code YB_CONN_MGR_PARAMETER_STATUS})
 * so Odyssey can track the server's parameter state. Such a packet is consumed
 * internally and must never be forwarded to the client. If the packet is larger
 * than the readahead buffer it is handed to the relay in multiple chunks;
 * Odyssey must reassemble the full packet before parsing it. Parsing a partial
 * packet fails with "failed to parse ParameterStatus message" and tears the
 * connection down.
 *
 * The test shrinks the readahead buffer so that even for GUCs set by pgJDBC
 * internally, the 'r' packet is split. We then assert that we don't get the
 * error message
 */
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestParameterStatusSplit extends BaseYsqlConnMgr {
  private static final int READAHEAD_BUFFER_SIZE = 96;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
    Map<String, String> flags = new HashMap<>();
    flags.put("ysql_conn_mgr_readahead_buffer_size",
        Integer.toString(READAHEAD_BUFFER_SIZE));
    builder.addCommonTServerFlags(flags);
  }

  @Test
  public void testLargeParameterStatusIsReassembled() throws Exception {
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_IDX);

    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .connect()) {
    }

    String parseFailure =
        tailer.waitForLogRegex("failed to parse ParameterStatus message", 10, TimeUnit.SECONDS);
    assertNull(
        "Connection manager logged a ParameterStatus parse failure, which means "
            + "an oversized 'r' packet was parsed before being fully reassembled: "
            + parseFailure,
        parseFailure);
  }
}
