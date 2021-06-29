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

import com.google.common.net.HostAndPort;
import org.yb.AssertionWrappers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestLoadBalance extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgEncryption.class);

  @Test
  public void testYBServersFunction() throws Exception {
    Statement st = connection.createStatement();
    ResultSet rs = st.executeQuery("select * from yb_servers()");
    int cnt = 0;
    Map<HostAndPort, MiniYBDaemon> hostPortsDaemonMap = miniCluster.getTabletServers();
    Map<String, Integer> hostPorts = new HashMap<>();
    for (Map.Entry<HostAndPort, MiniYBDaemon> e : hostPortsDaemonMap.entrySet()) {
      hostPorts.put(e.getKey().getHost(), e.getKey().getPort());
    }

    while (rs.next()) {
      String host = rs.getString(1);
      int port = rs.getInt(2);
      int connections = rs.getInt(3);
      String node_type = rs.getString(4);
      String cloud = rs.getString(5);
      String region = rs.getString(6);
      String zone = rs.getString(7);
      String publicIp = rs.getString(8);
      Integer portInMap = hostPorts.get(host);
      AssertionWrappers.assertNotNull(portInMap);
      AssertionWrappers.assertEquals("port should be equal", 5433, port);
      AssertionWrappers.assertEquals("primary", node_type);
      AssertionWrappers.assertEquals("connections has been hardcoded to 0", 0, connections);
      AssertionWrappers.assertEquals("cloud1", cloud);
      AssertionWrappers.assertEquals("datacenter1", region);
      AssertionWrappers.assertEquals("rack1", zone);
      AssertionWrappers.assertTrue(publicIp.isEmpty());
      cnt++;
    }
    AssertionWrappers.assertEquals(
      "expected servers started by minicluster", hostPortsDaemonMap.size(), cnt);

  }
}
