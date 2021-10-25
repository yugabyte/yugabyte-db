// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.URL;
import java.sql.*;
import java.util.List;
import java.util.Map;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import com.google.common.net.HostAndPort;
import org.yb.minicluster.MiniYBDaemon;
import com.yugabyte.util.PSQLException;
import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRpcForwarding extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgRpcForwarding.class);
  private static final int SLEEP_DURATION = 5000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_forward_rpcs_to_local_tserver", "true");
    return flags;
  }

  private int getForwardRpcCount() throws Exception {
    Map<HostAndPort, MiniYBDaemon> tservers = miniCluster.getTabletServers();
    int count = 0;

    for (Map.Entry<HostAndPort,MiniYBDaemon> entry : tservers.entrySet()) {
      HostAndPort hostPort = entry.getKey();
      int port = tservers.get(hostPort).getWebPort();

      // Call the prometheus-metrics endpoint and grep for the multi touch hit metric in the list.
      URL url = null;
      BufferedReader br = null;
      try {
        url = new URL(String.format("http://%s:%d/prometheus-metrics", hostPort.getHost(), port));
        br = new BufferedReader(new InputStreamReader(url.openStream()));;
      } catch (Exception ex) {
        LOG.error("Encountered error for reading metrics endpoint" + ex);
        throw new InternalError(ex.getMessage());
      }
      String line = null;
      while ((line = br.readLine()) != null) {
        if (line.contains("TabletServerForwardService_Write_count") ||
            line.contains("TabletServerForwardService_Read_count")) {
          String inp[] = line.split(" ");
          String x = inp[inp.length - 2].trim();
          int val = Integer.parseInt(x);
          count += val;
        }
      }
    }
    LOG.info("Forward service count is " + count);
    return count;

  }

  @Test
  public void testRpcForwarding() throws Exception {
    int numRows = 100;
    try (Statement statement = connection.createStatement()) {
      statement.execute("create table t(a int primary key, b int)");

      int count1 = getForwardRpcCount();
      for (int i = 1; i <= numRows; ++i) {
        statement.execute("insert into t values(" + i + "," + i + ")");
      }
      int count2 = getForwardRpcCount();
      assertTrue(count2 > count1);

      for (int i = 1; i <= numRows; ++i) {
        ResultSet rs = statement.executeQuery(String.format("select * from t where a=%d", i));
        while (rs.next()) {
          int a = rs.getInt("a");
          int b = rs.getInt("b");
          assertTrue(a == b);
        }
      }
      int count3 = getForwardRpcCount();
      assertTrue(count3 > count2);
    } catch (PSQLException ex) {
      LOG.error("Unexpected exception:", ex);
      throw ex;
    }
  }
}
