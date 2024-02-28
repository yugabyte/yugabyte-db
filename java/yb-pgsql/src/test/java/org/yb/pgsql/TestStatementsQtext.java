// Copyright (c) Yugabytedb, Inc.
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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.net.URL;
import java.sql.Statement;
import java.util.Map;
import java.util.Scanner;
import java.util.stream.Stream;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBDaemon;

@RunWith(value = YBTestRunner.class)
public class TestStatementsQtext extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestStatementsQtext.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_pg_conf_csv", "pg_stat_statements.yb_qtext_size_limit=1kB");
    return flagMap;
  }

  /**
   * Tests that when the query file is bigger than the limit, we return an empty list
   * on the /statements endpoint.
   */
  @Test
  public void testHugeQueryFileWithLimit() throws Exception {
    try (Statement statement = connection.createStatement()) {
      String hugeQuery = Stream.generate(() -> " AND TRUE = TRUE").limit(1000)
        .reduce("SELECT * FROM pg_class WHERE TRUE = TRUE", (acc, clause) -> acc + clause);

      statement.execute(hugeQuery);

      verifyStatementsEmpty();
    }
  }

  /**
   * Checks that the /statements endpoint returns an empty list of statements:
   * {
   *    "statements": []
   * }
   */
  private static void verifyStatementsEmpty() throws Exception {
    MiniYBDaemon ts = (MiniYBDaemon) miniCluster.getTabletServers().values().toArray()[0];
    URL url = new URL(
        String.format("http://%s:%d/statements", ts.getLocalhostIP(), ts.getPgsqlWebPort()));
    try (Scanner scanner = new Scanner(url.openConnection().getInputStream())) {
      JsonElement tree = JsonParser.parseString(scanner.useDelimiter("\\A").next());
      JsonObject obj = tree.getAsJsonObject();
      JsonArray statements_list = obj.getAsJsonArray("statements");
      assertEquals(0, statements_list.size());
    }
  }
}
