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

import java.net.URL;
import java.util.Scanner;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrLoggerMetrics extends BaseYsqlConnMgr {

  private String fetchPrometheusMetrics() throws Exception {
    String host = getPgHost(TSERVER_IDX);
    MiniYBDaemon ts = null;
    for (MiniYBDaemon daemon : miniCluster.getTabletServers().values()) {
      if (host.equals(daemon.getLocalhostIP())) {
        ts = daemon;
        break;
      }
    }
    assertNotNull(ts);
    String endpoint = String.format(
        "http://%s:%d/prometheus-metrics", ts.getLocalhostIP(), ts.getPgsqlWebPort());
    try (Scanner scanner = new Scanner(new URL(endpoint).openConnection().getInputStream())) {
      return scanner.useDelimiter("\\A").next();
    }
  }

  private static Long getMetric(String metrics, String name) {
    Matcher m = Pattern.compile("^" + name + "\\{[^}]*\\}\\s+(\\d+)", Pattern.MULTILINE)
        .matcher(metrics);
    return m.find() ? Long.parseLong(m.group(1)) : null;
  }

  // On an idle cluster the logger metrics are exported and show an empty queue and no drops.
  @Test
  public void testLoggerMetricsOnIdleCluster() throws Exception {
    TestUtils.waitFor(() -> {
      Long ts = getMetric(fetchPrometheusMetrics(), "ysql_conn_mgr_last_updated_timestamp");
      return ts != null && ts > 0;
    }, 30000);

    String metrics = fetchPrometheusMetrics();
    Long dropped = getMetric(metrics, "ysql_conn_mgr_logger_dropped_lines");
    Long queueSize = getMetric(metrics, "ysql_conn_mgr_logger_queue_size");
    assertNotNull("ysql_conn_mgr_logger_dropped_lines missing", dropped);
    assertNotNull("ysql_conn_mgr_logger_queue_size missing", queueSize);
    assertEquals(0L, dropped.longValue());
    assertLessThan(queueSize, 256L);
  }
}
