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
package org.yb.cql;

import java.util.*;
import java.util.stream.Collectors;
import java.text.SimpleDateFormat;

import com.datastax.driver.core.LocalDate;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.Statement;
import com.yugabyte.driver.core.TableSplitMetadata;
import com.yugabyte.driver.core.policies.PartitionAwarePolicy;
import org.junit.Test;

import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.RocksDBMetrics;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNull;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestSelectIf extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestSelectIf.class);

  private void createIfTestDB() throws Exception {
    String create_tab = "CREATE TABLE" +
                        "  tab(h INT, r INT, l INT, m INT, n INT, j JSONB, PRIMARY KEY (h, r))" +
                        "  WITH CLUSTERING ORDER BY (r ASC)" +
                        "       AND TRANSACTIONS = { 'enabled' : true};";
    String create_l_index = "CREATE INDEX lidx ON tab(l) INCLUDE(m);";
    String create_j_index = "CREATE INDEX jidx ON tab(j->>'l') INCLUDE(m);";

    session.execute(create_tab);
    session.execute(create_l_index);
    session.execute(create_j_index);

    // Insert rows where secondary key (L) is of value 1.
    //  h | r | l | m  | n   | j
    // ---+---+---+----+-----+------------------------
    //  1 | 1 | 1 | 13 | 113 | {"l":1,"m":13,"n":113}
    //  1 | 3 | 1 | 11 | 111 | {"l":1,"m":11,"n":111}
    //  1 | 5 | 1 | 13 | 113 | {"l":1,"m":13,"n":113}
    //  1 | 7 | 1 | 11 | 111 | {"l":1,"m":11,"n":111}
    //  1 | 9 | 1 | 13 | 113 | {"l":1,"m":13,"n":113}
    String insert_format =
      "INSERT INTO tab(h, r, l, m, n, j) VALUES (%d, %d, %d, %d, %d, '%s');";
    int h = 1;
    int r;
    int l = 1;
    int m = 11;
    int n;
    String j;
    for (r = 1; r < 10; r += 2) {
      m = (m == 11 ? 13 : 11);
      n = m + 100;
      j =  String.format("{\"l\":%d, \"m\":%d, \"n\":%d}", l, m, n);
      String insert = String.format(insert_format, h, r, l, m, n, j);
      session.execute(insert);
    }

    // Insert rows where secondary key (L) is of value 2.
    //  h | r | l | m  | n   | j
    // ---+---+---+----+-----+------------------------
    //  1 | 0 | 2 | 24 | 224 | {"l":2,"m":24,"n":224}
    //  1 | 2 | 2 | 22 | 222 | {"l":2,"m":22,"n":222}
    //  1 | 4 | 2 | 24 | 224 | {"l":2,"m":24,"n":224}
    //  1 | 6 | 2 | 22 | 222 | {"l":2,"m":22,"n":222}
    //  1 | 8 | 2 | 24 | 224 | {"l":2,"m":24,"n":224}
    l = 2;
    m = 22;
    for (r = 0; r < 10; r += 2) {
      m = (m == 22 ? 24 : 22);
      n = m + 200;
      j =  String.format("{\"l\":%d, \"m\":%d, \"n\":%d}", l, m, n);
      String insert = String.format(insert_format, h, r, l, m, n, j);
      session.execute(insert);
    }
  }

  @Test
  public void testIfClauseForPrimitiveType() throws Exception {
    String expected_result = "";
    String result = "";

    // Set up tables and indexes.
    createIfTestDB();

    // =============================================================================================
    // All of the following queries should yield the same result set with
    //   SELECT r FROM tab WHERE l = 1;
    //    r
    //   ---
    //    1
    //    3
    //    5
    //    7
    //    9
    // =============================================================================================
    expected_result = runValidSelect("SELECT r FROM tab WHERE l = 1;");
    LOG.info("Suite 1 - Expected Result = " + expected_result);

    result = runValidSelect("SELECT r FROM tab IF l != 2;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF m != 0;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n != 0;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF m != 0 AND n != 0;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF m = 11 OR m = 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n != 111 OR m != 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n = 111 OR m = 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 2-1 IF n = 100+11 OR m = 10+3;");
    assertEquals(expected_result, result);

    // Using Json index.
    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF j->>'l' != '2';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF m != 0;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n != 0;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF m != 0 AND n != 0;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF m = 11 OR m = 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n != 111 OR m != 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n = 111 OR m = 13;");
    assertEquals(expected_result, result);

    // =============================================================================================
    // All of the following queries should yield the same result set with
    //   SELECT r FROM tab WHERE l = 1 AND m = 11;
    //    r
    //   ---
    //    3
    //    7
    // =============================================================================================
    expected_result = runValidSelect("SELECT r FROM tab WHERE l = 1 AND m = 11 ALLOW FILTERING;");
    LOG.info("Suite 2 - Expected Result = " + expected_result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF l = 1 AND m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF l < 2 AND m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF n = 111;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF m != 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n = 111;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n != 113;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n = 111 AND m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n != 113 AND m != 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF m = 11 OR n = 111;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF m != 13 OR n != 113;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n = 111 OR m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF n != 113 OR m != 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 2-1 IF n != 100+13 OR m != 10+3;");
    assertEquals(expected_result, result);

    // Using Json index.
    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' AND m = 11 ALLOW FILTERING;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF j->>'l' = '1' AND m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF j->>'l' < '2' AND m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF m != 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n = 111;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n != 113;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n = 111 AND m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n != 113 AND m != 13;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF m = 11 OR n = 111;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF m != 13 OR n != 113;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n = 111 OR m = 11;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF n != 113 OR m != 13;");
    assertEquals(expected_result, result);
  }

  @Test
  public void testIfClauseForJsonType() throws Exception {
    String expected_result = "";
    String result = "";

    // Set up tables and indexes.
    createIfTestDB();

    // =============================================================================================
    // All of the following queries should yield the same result set with
    //   SELECT r FROM tab WHERE l = 1;
    // =============================================================================================
    expected_result = runValidSelect("SELECT r FROM tab WHERE l = 1;");
    LOG.info("Suite 1 - Expected Result = " + expected_result);

    result = runValidSelect("SELECT r FROM tab IF l != 2;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'m' != '0';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'n' != '0';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'m' != '0' AND j->>'n' != '0';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'m' = '11' OR j->>'m' = '13';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1" +
                            " IF j->>'n' != '111' OR j->>'m' != '13';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'n' = '111' OR j->>'m' = '13';");
    assertEquals(expected_result, result);

    // Using Json index.
    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF j->>'l' != '2';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF j->>'m' != '0';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF j->>'n' != '0';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'m' != '0' AND j->>'n' != '0';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'m' = '11' OR j->>'m' = '13';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'n' != '111' OR j->>'m' != '13';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'n' = '111' OR j->>'m' = '13';");
    assertEquals(expected_result, result);

    // =============================================================================================
    // All of the following queries should yield the same result set with
    //   SELECT r FROM tab WHERE l = 1 AND m = 11;
    // =============================================================================================
    expected_result = runValidSelect("SELECT r FROM tab WHERE l = 1 AND m = 11 ALLOW FILTERING;");
    LOG.info("Suite 2 - Expected Result = " + expected_result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 AND j->>'m' = '11'" +
                            " ALLOW FILTERING;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF l = 1 AND j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF l < 2 AND j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF j->>'n' = '111';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'m' != '13';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'n' = '111';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'n' != '113';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'n' = '111' AND j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1" +
                            "  IF j->>'n' != '113' AND j->>'m' != '13';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1" +
                            "  IF j->>'m' = '11' OR j->>'n' = '111';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1" +
                            " IF j->>'m' != '13' OR j->>'n' != '113';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1 IF j->>'n' = '111' OR j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE l = 1" +
                            " IF j->>'n' != '113' OR j->>'m' != '13';");
    assertEquals(expected_result, result);

    // Using Json index.
    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' AND j->>'m' = '11'" +
                            "  ALLOW FILTERING;");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF j->>'l' = '1' AND j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab IF j->>'l' < '2' AND j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF j->>'m' != '13';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF j->>'n' = '111';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1' IF j->>'n' != '113';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'n' = '111' AND j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'n' != '113' AND j->>'m' != '13';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'m' = '11' OR j->>'n' = '111';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            " IF j->>'m' != '13' OR j->>'n' != '113';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'n' = '111' OR j->>'m' = '11';");
    assertEquals(expected_result, result);

    result = runValidSelect("SELECT r FROM tab WHERE j->>'l' = '1'" +
                            "  IF j->>'n' != '113' OR j->>'m' != '13';");
    assertEquals(expected_result, result);
  }
}
