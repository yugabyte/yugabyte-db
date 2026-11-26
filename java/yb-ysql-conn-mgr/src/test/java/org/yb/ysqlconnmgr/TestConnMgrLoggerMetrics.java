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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrLoggerMetrics extends BaseYsqlConnMgr {

  // On an idle cluster the logger metrics are exported, report no drops, and show a queue below
  // the logger's 256-line limit.
  @Test
  public void testLoggerMetricsOnIdleCluster() throws Exception {
    TestUtils.waitFor(() -> {
      Long ts = getPrometheusMetric(getPrometheusMetrics(), "ysql_conn_mgr_last_updated_timestamp");
      return ts != null && ts > 0;
    }, 30000);

    String metrics = getPrometheusMetrics();
    Long dropped = getPrometheusMetric(metrics, "ysql_conn_mgr_logger_dropped_lines");
    Long queueSize = getPrometheusMetric(metrics, "ysql_conn_mgr_logger_queue_size");
    assertNotNull("ysql_conn_mgr_logger_dropped_lines missing", dropped);
    assertNotNull("ysql_conn_mgr_logger_queue_size missing", queueSize);
    assertEquals(0L, dropped.longValue());
    assertLessThan(queueSize, 256L);
  }
}
