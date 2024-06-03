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

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.Metrics;
import org.yb.YBTestRunner;

import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value=YBTestRunner.class)
public class TestPgSelectLimit extends BasePgSQLTest {

  private static final String kTableName = "single_tablet_table";
  private static final int kLimit = 15;
  private static final int kDefaultPrefetchLimit = 100;

  // Start server in RF=1 mode to simplify metrics analysis.
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_prefetch_limit", Integer.toString(kDefaultPrefetchLimit));
    flagMap.put("ysql_enable_packed_row", "true");
    return flagMap;
  }

  int getRocksdbNextFoundMetric() throws Exception {
    // Wait for stable metric value.
    int stableMetricCount = 0;
    int resultCandidate = -1;
    long deadline = System.currentTimeMillis() + 10000;
    while (System.currentTimeMillis() < deadline) {
      JsonArray[] metrics = getRawTSMetric();
      assertEquals(1, metrics.length);
      for (JsonElement el : metrics[0]) {
        JsonObject obj = el.getAsJsonObject();
        // Suppose the is the only table with range partition.
        if (obj.get("type").getAsString().equals("tablet") &&
            obj.getAsJsonObject("attributes").get("table_name").getAsString().equals(kTableName)) {
          final int result = new Metrics(obj).getCounter("rocksdb_number_db_next_found").value;
          if (result == resultCandidate) {
            if(++stableMetricCount > 3) {
              return result;
            }
          } else {
            stableMetricCount = 0;
            resultCandidate = result;
          }
          break;
        }
      }
      Thread.sleep(200);
    }
    return 0;
  }

  @Test
  public void testRocksdbReadForLimit() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s(k INT, PRIMARY KEY(k ASC))", kTableName));
      stmt.execute(String.format(
          "INSERT INTO %s SELECT * FROM GENERATE_SERIES(1, 2000)", kTableName));
      // Recently (D26964) we started to use Next to move to check presence of the next column after
      // packed row. Instead of Seek that was used previously. Because of that we get one extra Next
      // in this test.
      executeQueryWithMetricCheck(
          stmt,
          String.format("SELECT * FROM %s LIMIT %d", kTableName, kLimit),
          kLimit + 1);
      // In case query has WHERE clause default prefetch strategy is used.
      // I.e. default prefetch limit + prefetch next portion of data after returning first one.
      executeQueryWithMetricCheck(
          stmt,
          String.format("SELECT * FROM %s WHERE CASE k WHEN 10 THEN false ELSE true END LIMIT %d",
                        kTableName, kLimit),
          kDefaultPrefetchLimit * 2 + 2);
    }
  }

  private void executeQueryWithMetricCheck(Statement stmt,
                                           String query,
                                           int expectedMetric) throws Exception {
    final int startMetric = getRocksdbNextFoundMetric();
    stmt.execute(query);
    assertEquals(expectedMetric, getRocksdbNextFoundMetric() - startMetric);
  }
}
