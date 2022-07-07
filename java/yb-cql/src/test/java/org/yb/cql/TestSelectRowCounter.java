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

import org.junit.BeforeClass;
import org.junit.Test;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.ColumnDefinitions.Definition;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSetFuture;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.TableMetadata;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.RocksDBMetrics;
import org.yb.util.BuildTypeUtil;
import org.yb.util.TableProperties;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestSelectRowCounter extends BaseCQLTest {
  @Override
  public int getTestMethodTimeoutSec() {
    // Usual time for a test ~90 seconds. But can be much more on Jenkins.
    return super.getTestMethodTimeoutSec()*10;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("allow_index_table_read_write", "true");
    flagMap.put("index_backfill_upperbound_for_user_enforced_txn_duration_ms", "1000");
    flagMap.put("index_backfill_wait_for_old_txns_ms", "100");
    return flagMap;
  }

  private void CreateTableWithData() throws Exception {
    // Create table.
    session.execute("CREATE TABLE test_counter(h INT, r INT, i INT, j INT, k INT, " +
                    "  PRIMARY KEY (h, r));");

    // Create index.
    session.execute("CREATE INDEX test_counter_idx on test_counter (i, j, k)" +
                    "  WITH transactions = {'enabled': 'false', "+
                    "                       'consistency_level': 'user_enforced'};");

    // Insert data.
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (1, 0, 100, 9, 0);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (1, 1, 100, 8, 1);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (1, 2, 100, 7, 2);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (1, 3, 200, 6, 3);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (1, 4, 200, 5, 4);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (2, 5, 300, 4, 5);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (2, 6, 300, 3, 6);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (2, 7, 400, 2, 7);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (2, 8, 400, 1, 8);");
    session.execute("INSERT INTO test_counter (h, r, i, j, k) VALUES (2, 9, 400, 0, 9);");
  }

  @Test
  public void testRowCounterPrimaryScan() throws Exception {
    CreateTableWithData();

    // Check LIMIT and OFFSET for PRIMARY scan.
    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 0;", "");

    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 4;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]")));

    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 5;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]")));

    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 6;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]",
                                                  "Row[5, 4]")));

    // Check OFFSET for PRIMARY scan.
    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) OFFSET 0;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]",
                                                  "Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) OFFSET 4;",
                new HashSet<String>(Arrays.asList("Row[4, 5]",
                                                  "Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) OFFSET 5;",
                new HashSet<String>(Arrays.asList("Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) OFFSET 6;",
                new HashSet<String>(Arrays.asList("Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    // Check LIMIT and OFFSET for PRIMARY scan.
    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 3 OFFSET 4;",
                new HashSet<String>(Arrays.asList("Row[4, 5]",
                                                  "Row[5, 4]",
                                                  "Row[6, 3]")));

    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 3 OFFSET 5;",
                new HashSet<String>(Arrays.asList("Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]")));

    assertQuery("SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 3 OFFSET 6;",
                new HashSet<String>(Arrays.asList("Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]")));
  }

  @Test
  public void testRowCounterSecondaryScan() throws Exception {
    CreateTableWithData();

    // Check LIMIT for SECONDARY scan.
    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 0;", "");

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 4;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",    // i == 100
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[4, 5]"))); // i == 200

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 5;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",    // i == 100
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]",    // i == 200
                                                  "Row[4, 5]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 6;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",    // i == 100
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]",    // i == 200
                                                  "Row[4, 5]",
                                                  "Row[6, 3]"))); // i == 300

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 10;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]",
                                                  "Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 11;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]",
                                                  "Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    // Check OFFSET for SECONDARY scan.
    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) OFFSET 0;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]",
                                                  "Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) OFFSET 2;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]",
                                                  "Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) OFFSET 5;",
                new HashSet<String>(Arrays.asList("Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) OFFSET 6;",
                new HashSet<String>(Arrays.asList("Row[5, 4]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) OFFSET 7;",
                new HashSet<String>(Arrays.asList("Row[7, 2]",
                                                  "Row[8, 1]",
                                                  "Row[9, 0]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) OFFSET 8;",
                new HashSet<String>(Arrays.asList("Row[7, 2]",
                                                  "Row[8, 1]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) OFFSET 9;",
                new HashSet<String>(Arrays.asList("Row[7, 2]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) OFFSET 10;", "");

    // Check LIMIT and OFFSET for SECONDARY scan.
    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 3 OFFSET 0;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 3 OFFSET 1;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[4, 5]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 3 OFFSET 2;",
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 3 OFFSET 8;",
                new HashSet<String>(Arrays.asList("Row[7, 2]",
                                                  "Row[8, 1]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 3 OFFSET 9;",
                new HashSet<String>(Arrays.asList("Row[7, 2]")));

    assertQuery("SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) LIMIT 3 OFFSET 10;",
                "");
  }

  @Test
  public void testRowCounterPaging() throws Exception {
    CreateTableWithData();

    // Use PRIMARY index with page size 1. No need to test for larger page size.
    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 3 OFFSET 4;")
                    .setFetchSize(1),
                new HashSet<String>(Arrays.asList("Row[4, 5]",
                                                  "Row[5, 4]",
                                                  "Row[6, 3]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 3 OFFSET 5;")
                    .setFetchSize(1),
                new HashSet<String>(Arrays.asList("Row[5, 4]",
                                                  "Row[6, 3]",
                                                  "Row[7, 2]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE h IN (1, 2) LIMIT 3 OFFSET 6;")
                    .setFetchSize(1),
                new HashSet<String>(Arrays.asList("Row[6, 3]",
                                                  "Row[7, 2]",
                                                  "Row[8, 1]")));

    // Use SECONDARY index with page size 1.
    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 0;")
                    .setFetchSize(1),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 1;")
                    .setFetchSize(1),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[4, 5]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 2;")
                    .setFetchSize(1),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) " +
                    "LIMIT 3 OFFSET 8;")
                    .setFetchSize(1),
                new HashSet<String>(Arrays.asList("Row[7, 2]",
                                                  "Row[8, 1]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 9;")
                    .setFetchSize(1),
                new HashSet<String>(Arrays.asList("Row[7, 2]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 10;")
                    .setFetchSize(1),
                "");

    // Use SECONDARY index with (page size == LIMIT)
    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 0;")
                    .setFetchSize(3),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 1;")
                    .setFetchSize(3),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[4, 5]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 2;")
                    .setFetchSize(3),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) " +
                    "LIMIT 3 OFFSET 8;")
                    .setFetchSize(3),
                new HashSet<String>(Arrays.asList("Row[7, 2]",
                                                  "Row[8, 1]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 9;")
                    .setFetchSize(3),
                new HashSet<String>(Arrays.asList("Row[7, 2]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 10;")
                    .setFetchSize(3),
                "");

    // Use SECONDARY index with (page size > LIMIT)
    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 0;")
                    .setFetchSize(4),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[2, 7]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 1;")
                    .setFetchSize(4),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[1, 8]",
                                                  "Row[4, 5]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 2;")
                    .setFetchSize(4),
                new HashSet<String>(Arrays.asList("Row[0, 9]",
                                                  "Row[3, 6]",
                                                  "Row[4, 5]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400) " +
                    "LIMIT 3 OFFSET 8;")
                    .setFetchSize(4),
                new HashSet<String>(Arrays.asList("Row[7, 2]",
                                                  "Row[8, 1]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 9;")
                    .setFetchSize(4),
                new HashSet<String>(Arrays.asList("Row[7, 2]")));

    assertQuery(new SimpleStatement(
                    "SELECT k, j FROM test_counter WHERE i IN (100, 200, 300, 400)" +
                    "  LIMIT 3 OFFSET 10;")
                    .setFetchSize(4),
                "");
  }

  // This test is provided by user on github issue #7055 with some additions.
  // - It runs index-scan and then runs filter.
  // - LIMIT and OFFSET must be applied after filter operations.
  @Test
  public void testRowCounterFilter() throws Exception {
    // Create table.
    session.execute("CREATE TABLE test_filter(id INT, scope TEXT, metadata MAP<TEXT, TEXT>, " +
                    "    PRIMARY KEY (id)) " +
                    "  WITH default_time_to_live = 0" +
                    "    AND transactions = {'enabled': 'false'};");

    // Create index.
    session.execute("CREATE INDEX test_filter_index ON test_filter(scope, id)" +
                    "  WITH CLUSTERING ORDER BY (id ASC)" +
                    "    AND transactions = {'enabled': 'false', " +
                    "      'consistency_level' : 'user_enforced'};");

    // Insert data.
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (1, 'test', {'a' : '1', 'b' : '1'});");
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (2, 'test', {'a' : '2', 'b' : '2'});");
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (3, 'test', {'a' : '3', 'b' : '3'});");
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (4, 'test', {'a' : '4', 'b' : '4'});");
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (5, 'test', {'a' : '5', 'b' : '5'});");

    // Select from test_filter.
    assertQuery("SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5' LIMIT 1;",
                "Row[5]");
    assertQuery("SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5' LIMIT 5;",
                "Row[5]");

    // Add a few more rows to test OFFSET.
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (6, 'test', {'a' : '5', 'b' : '5'});");
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (7, 'test', {'a' : '5', 'b' : '5'});");
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (8, 'test', {'a' : '5', 'b' : '5'});");
    session.execute("INSERT into test_filter(id, scope, metadata)" +
                    "  VALUES (9, 'test', {'a' : '5', 'b' : '5'});");

    assertQuery("SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                "  LIMIT 1 OFFSET 1;",
                "Row[6]");
    assertQuery("SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                "  LIMIT 1 OFFSET 4;",
                "Row[9]");
    assertQuery("SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                "  LIMIT 1 OFFSET 5;",
                "");

    assertQuery("SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                "  LIMIT 2 OFFSET 1;",
                "Row[6]Row[7]");
    assertQuery("SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                "  LIMIT 2 OFFSET 3;",
                "Row[8]Row[9]");
    assertQuery("SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                "  LIMIT 2 OFFSET 5;",
                "");

    // Test page-size setting with all of above cases.
    assertQuery(new SimpleStatement(
                    "SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                    "  LIMIT 1;")
                    .setFetchSize(1),
                "Row[5]");
    assertQuery(new SimpleStatement(
                    "SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                    "  LIMIT 5;")
                    .setFetchSize(1),
                "Row[5]Row[6]Row[7]Row[8]Row[9]");

    assertQuery(new SimpleStatement(
                    "SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                    "  LIMIT 1 OFFSET 1;")
                    .setFetchSize(1),
                "Row[6]");
    assertQuery(new SimpleStatement(
                    "SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                    "  LIMIT 1 OFFSET 4;")
                    .setFetchSize(1),
                "Row[9]");
    assertQuery(new SimpleStatement(
                    "SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                    "  LIMIT 1 OFFSET 5;")
                    .setFetchSize(1),
                "");

    assertQuery(new SimpleStatement(
                    "SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                    "  LIMIT 2 OFFSET 1;")
                    .setFetchSize(1),
                "Row[6]Row[7]");
    assertQuery(new SimpleStatement(
                    "SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                    "  LIMIT 2 OFFSET 3;")
                    .setFetchSize(1),
                "Row[8]Row[9]");
    assertQuery(new SimpleStatement(
                    "SELECT id FROM test_filter WHERE scope = 'test' AND metadata['a'] = '5'" +
                    "  LIMIT 2 OFFSET 5;")
                    .setFetchSize(1),
                "");
  }
}
