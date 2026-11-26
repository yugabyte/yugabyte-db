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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.net.URL;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;
import java.util.Scanner;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBDaemon;

@RunWith(value = YBTestRunner.class)
public class TestStatementsQtextTruncation extends BasePgSQLTest {
  private static final int MAX_QUERY_TEXT_LENGTH = 64;
  private static final int NUM_ROWS = 2000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "pg_stat_statements.yb_qtext_size_limit=4kB");
    appendToYsqlPgConf(flagMap,
        "pg_stat_statements.yb_max_query_text_length=" + MAX_QUERY_TEXT_LENGTH);
    return flagMap;
  }

  /**
   * A batched insert with thousands of bind parameters has a query text several times larger
   * than the 4kB query text file limit. Its stored text is truncated, so the file stays under the
   * limit and both the view and the /statements endpoint keep returning query texts.
   */
  @Test
  public void testBatchedInsertTextIsTruncated() throws Exception {
    // A multi-row insert with two bind parameters per row.
    StringBuilder sql = new StringBuilder("INSERT INTO batch_t VALUES ");
    // The same insert as the server receives it: the driver sends each "?" as "$n".
    StringBuilder sentSql = new StringBuilder(sql);
    for (int i = 0; i < NUM_ROWS; i++) {
      String sep = i == 0 ? "" : ", ";
      sql.append(sep).append("(?, ?)");
      sentSql.append(sep).append(String.format("($%d, $%d)", 2 * i + 1, 2 * i + 2));
    }
    // Only the first MAX_QUERY_TEXT_LENGTH bytes are stored, followed by "...".
    String expectedText = sentSql.substring(0, MAX_QUERY_TEXT_LENGTH) + "...";

    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE TABLE batch_t (a int, b int)");
      // Start from an empty query text file.
      stmt.execute("SELECT pg_stat_statements_reset()");

      // Run the batch as a prepared statement, the way drivers send batched inserts.
      try (PreparedStatement insert = conn.prepareStatement(sql.toString())) {
        for (int i = 1; i <= 2 * NUM_ROWS; i++) {
          insert.setInt(i, i);
        }
        assertEquals(NUM_ROWS, insert.executeUpdate());
      }

      // Every query text reads as NULL once the file exceeds yb_qtext_size_limit, so finding the
      // truncated text also shows that the file stayed under the limit.
      try (ResultSet rs = stmt.executeQuery(
          "SELECT query FROM pg_stat_statements WHERE query LIKE 'INSERT INTO batch_t%'")) {
        assertTrue(rs.next());
        assertEquals(expectedText, rs.getString("query"));
        assertFalse(rs.next());
      }
    }

    // The /statements endpoint reads the same file, so it returns the truncated text too.
    assertTrue(statementsEndpointHasQuery(expectedText));
  }

  /** Returns true if the /statements endpoint of any tserver lists the given query text. */
  private static boolean statementsEndpointHasQuery(String query) throws Exception {
    // Only the tserver that ran the insert has it, so check all of them.
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      URL url = new URL(
          String.format("http://%s:%d/statements", ts.getLocalhostIP(), ts.getPgsqlWebPort()));
      try (Scanner scanner = new Scanner(url.openConnection().getInputStream())) {
        JsonObject obj = JsonParser.parseString(scanner.useDelimiter("\\A").next())
            .getAsJsonObject();
        JsonArray statements = obj.getAsJsonArray("statements");
        for (JsonElement statement : statements) {
          if (query.equals(statement.getAsJsonObject().get("query").getAsString())) {
            return true;
          }
        }
      }
    }
    return false;
  }
}
