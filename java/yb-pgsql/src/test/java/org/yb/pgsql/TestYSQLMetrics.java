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

import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBDaemon;


import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.net.URL;
import java.net.MalformedURLException;
import java.util.Scanner;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonArray;
import com.google.gson.JsonParser;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestYSQLMetrics extends BasePgSQLTest {
      private static final Logger LOG = LoggerFactory.getLogger(MiniYBDaemon.class);

  public int getMetricCounter(String metricName) throws Exception {
    int value = 0;
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      URL url = new URL(String.format("http://%s:%d/metrics",
                                      ts.getLocalhostIP(),
                                      ts.getPgsqlWebPort()));
      Scanner scanner = new Scanner(url.openConnection().getInputStream());
      JsonParser parser = new JsonParser();
      JsonElement tree = parser.parse(scanner.useDelimiter("\\A").next());
      JsonObject obj = tree.getAsJsonArray().get(0).getAsJsonObject();
      assertEquals(obj.get("type").getAsString(), "server");
      assertEquals(obj.get("id").getAsString(), "yb.ysqlserver");
      value += new Metrics(obj).getYSQLMetric(metricName).count;
    }
    return value;
  }

  @Test
  public void testMetrics() throws Exception {
    Statement statement = connection.createStatement();

    String metric_prefix = "handler_latency_yb_ysqlserver_SQLProcessor_";

    int oldvalue = getMetricCounter(metric_prefix + "OtherStmts");
    statement.execute("CREATE TABLE test (col int)");
    int newvalue = getMetricCounter(metric_prefix + "OtherStmts");
    assertEquals(newvalue, oldvalue + 1);

    oldvalue = getMetricCounter(metric_prefix + "SelectStmt");
    statement.execute("SELECT * FROM test");
    newvalue = getMetricCounter(metric_prefix + "SelectStmt");
    assertEquals(newvalue, oldvalue + 1);

    oldvalue = getMetricCounter(metric_prefix + "InsertStmt");
    statement.execute("INSERT INTO test VALUES (1)");
    newvalue = getMetricCounter(metric_prefix + "InsertStmt");
    assertEquals(newvalue, oldvalue + 1);

    oldvalue = getMetricCounter(metric_prefix + "UpdateStmt");
    statement.execute("UPDATE test SET col = 2 WHERE col = 1");
    newvalue = getMetricCounter(metric_prefix + "UpdateStmt");
    assertEquals(newvalue, oldvalue + 1);

    oldvalue = getMetricCounter(metric_prefix + "DeleteStmt");
    statement.execute("DELETE FROM test");
    newvalue = getMetricCounter(metric_prefix + "DeleteStmt");
    assertEquals(newvalue, oldvalue + 1);

    oldvalue = getMetricCounter(metric_prefix + "InsertStmt");
    runInvalidQuery(statement, "INSERT INTO invalid_table VALUES (1)");
    newvalue = getMetricCounter(metric_prefix + "InsertStmt");
    assertEquals(newvalue, oldvalue);
  }
}
