/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * The following only applies to changes made to this file as part of YugaByte development.
 *
 *     Portions Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the
 * License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied.  See the License for the specific language governing permissions
 * and limitations under the License.
 */
package org.yb.cql;

import static org.yb.AssertionWrappers.*;

import java.util.*;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestIndexSelection extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestIndexSelection.class);

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

  @Before
  public void setUpTest() throws Exception {
    createKeyspace("yugatest");
    useKeyspace("yugatest");
  }

  // Test issue where PRIMARY INDEX should be chosen over SECONDARY INDEX, but it is not.
  @Test
  public void testPrimaryIndexScan() throws Exception {
    //----------------------------------------------------------------------------------------------
    // Test for single read.
    // Table.
    session.execute("CREATE TABLE test_primary_index" +
                    "  (i INT, j INT, x INT, y INT, PRIMARY KEY (i))" +
                    "  WITH TRANSACTIONS = {'enabled' : true};");

    // Index.
    session.execute("CREATE INDEX second_idx ON test_primary_index(x);");

    // Insert data.
    session.execute("INSERT INTO test_primary_index(i, j, x, y) VALUES(1, 1, 3, 3);");
    session.execute("INSERT INTO test_primary_index(i, j, x, y) VALUES(2, 2, 2, 2);");
    session.execute("INSERT INTO test_primary_index(i, j, x, y) VALUES(3, 3, 1, 1);");

    // Primary lookup should be chosen.
    assertQuery("EXPLAIN SELECT * FROM test_primary_index WHERE i = 1 AND x = 1;",
                "Row[Primary Key Lookup on yugatest.test_primary_index]" +
                "Row[  Key Conditions: (i = 1)                        ]" +
                "Row[  Filter: (x = 1)                                ]");

    assertQuery("SELECT * FROM test_primary_index WHERE i = 1 AND x = 1;", "");
    assertQuery("SELECT * FROM test_primary_index WHERE i = 2 AND x = 2;", "Row[2, 2, 2, 2]");

    //----------------------------------------------------------------------------------------------
    // Test for range scan.
    // Table
    if (false) {
      // TODO(neil) Comment this out for now.
      // Currently, there are tests that expect SECONDARY INDEX to be chosen over PRIMARY.
      // I don't yet understand their logic, so this test is commented out for now.
      session.execute("CREATE TABLE test_primary_range" +
                      "  (i INT, j INT, x INT, y INT, v1 INT, v2 INT, PRIMARY KEY (i, j))" +
                      "  WITH TRANSACTIONS = {'enabled' : true};");

      // Index.
      session.execute("CREATE INDEX second_range ON test_primary_range(x, y);");

      // Insert data.
      session.execute("INSERT INTO test_primary_range(i, j, x, y, v1, v2)" +
                      "  VALUES(1, 1, 1, 3, 101, 103);");
      session.execute("INSERT INTO test_primary_range(i, j, x, y, v1, v2)" +
                      "  VALUES(1, 2, 1, 2, 102, 102);");
      session.execute("INSERT INTO test_primary_range(i, j, x, y, v1, v2)" +
                      "   VALUES(1, 3, 1, 1, 103, 101);");

      // Primary range scan should be chosen.
      assertQuery("EXPLAIN SELECT * FROM test_primary_range WHERE i = 1 AND x = 1;",
                  "Row[Range Scan on yugatest.test_primary_range]" +
                  "Row[  Key Conditions: (i = 1)                ]" +
                  "Row[  Filter: (x = 1)                        ]");

      assertQuery("SELECT * FROM test_primary_range WHERE i = 1 AND x = 1 LIMIT 2;",
                  "Row[1, 1, 1, 3, 101, 103]" +
                  "Row[1, 2, 1, 2, 102, 102]");

      assertQuery("SELECT * FROM test_primary_range WHERE x = 1 LIMIT 2;",
                  "Row[1, 3, 1, 1, 103, 101]" +
                  "Row[1, 2, 1, 2, 102, 102]");
    }
  }

  // Test issues where PRIMARY KEY condition was wrongly enforced including github #7069.
  @Test
  public void testSecondaryIndexScan() throws Exception {
    // Table.
    session.execute("CREATE TABLE test_secondary_index" +
                    "  ( h1 INT, h2 INT, r INT, i1 INT, i2 INT, j INT, val INT," +
                    "    PRIMARY KEY ((h1, h2), r) )" +
                    "  WITH transactions = {'enabled' : true};");
    // Index.
    session.execute("CREATE INDEX second_idx ON test_secondary_index((i1, i2), j);");

    waitForReadPermsOnAllIndexes("yugatest", "test_secondary_index");

    // Insert data.
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (1, 1, 1, 1, 1, 1, 101);");
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (1, 2, 2, 1, 2, 2, 102);");
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (1, 3, 3, 1, 3, 3, 103);");
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (2, 1, 4, 2, 1, 4, 104);");
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (2, 2, 5, 2, 2, 5, 105);");
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (2, 3, 6, 2, 3, 6, 106);");
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (3, 1, 7, 3, 1, 7, 107);");
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (3, 2, 8, 3, 2, 8, 108);");
    session.execute("INSERT INTO test_secondary_index(h1, h2, r, i1, i2, j, val)" +
                    "  VALUES (3, 3, 9, 3, 3, 9, 109);");

    // Make sure secondary index is chosen.
    assertQuery("EXPLAIN SELECT * FROM test_secondary_index" +
                "  WHERE h1 IN (1, 2, 3) AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 0 AND val < 110;",
                "Row[Index Scan using yugatest.second_idx on yugatest.test_secondary_index]" +
                "Row[  Key Conditions: (i1 IN expr) AND (i2 = 2)                          ]" +
                "Row[  Filter: (h1 IN expr) AND (r > 0)                                   ]");

    // Check that dataset is correct.
    assertQuery("SELECT * FROM test_secondary_index" +
                "  WHERE h1 = 1 AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 0 AND val < 110;",
                "Row[1, 2, 2, 1, 2, 2, 102]");
    assertQuery("SELECT * FROM test_secondary_index" +
                "  WHERE h1 > 3 AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 0 AND val < 110;",
                "");
    assertQuery("SELECT * FROM test_secondary_index" +
                "  WHERE h1 < 5 AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 0 AND val < 110;",
                "Row[1, 2, 2, 1, 2, 2, 102]" +
                "Row[2, 2, 5, 2, 2, 5, 105]" +
                "Row[3, 2, 8, 3, 2, 8, 108]");

    assertQuery("SELECT * FROM test_secondary_index" +
                "  WHERE h1 IN (1, 2, 3) AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 3 AND val < 110;",
                "Row[2, 2, 5, 2, 2, 5, 105]" +
                "Row[3, 2, 8, 3, 2, 8, 108]");
    assertQuery("SELECT * FROM test_secondary_index" +
                "  WHERE h1 IN (1, 2, 3) AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 6 AND val < 110;",
                "Row[3, 2, 8, 3, 2, 8, 108]");
    assertQuery("SELECT * FROM test_secondary_index" +
                "  WHERE h1 IN (1, 2, 3) AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 9 AND val < 110;",
                "");

    assertQuery("SELECT * FROM test_secondary_index" +
                "  WHERE h1 IN (1, 2, 3) AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 0 AND val > 102;",
                "Row[2, 2, 5, 2, 2, 5, 105]" +
                "Row[3, 2, 8, 3, 2, 8, 108]");
    assertQuery("SELECT * FROM test_secondary_index" +
                "  WHERE h1 IN (1, 2, 3) AND i1 IN (1, 2, 3) AND i2 = 2 AND r > 0 AND val < 105;",
                "Row[1, 2, 2, 1, 2, 2, 102]");
  }

  @Test
  public void testNullsInIndexScan() throws Exception {
    // Table.
    session.execute("CREATE TABLE test_secondary_index1" +
                    "  ( h1 INT, r1 INT, i1 INT, i2 INT, " +
                    "    PRIMARY KEY (h1, r1) )" +
                    "  WITH transactions = {'enabled' : true}");
    // Index.
    session.execute("CREATE INDEX second_idx1 ON test_secondary_index1(r1, i1, i2)");

    waitForReadPermsOnAllIndexes("yugatest", "test_secondary_index1");

    // Insert data.
    session.execute("INSERT INTO test_secondary_index1(h1, r1, i1, i2)" +
                    "  VALUES (1, 2, null, 3)");

    // Make sure secondary index is chosen.
    assertQuery("EXPLAIN SELECT * FROM test_secondary_index1" +
                "  WHERE r1 = 2 AND i1 = null AND i2 IN (1,3,2)",
                "Row[Index Only Scan using yugatest.second_idx1 on" +
                " yugatest.test_secondary_index1]" +
                "Row[  Key Conditions: (r1 = 2)" +
                "                                                  ]" +
                "Row[  Filter: (i1 = NULL) AND (i2 IN expr)" +
                "                                      ]");

    assertQuery("SELECT * FROM test_secondary_index1 WHERE r1 = 2 AND i1 = null AND i2 IN (1,3,2)",
                "Row[1, 2, NULL, 3]");
  }

  @Test
  public void testOrderByIndexScan() throws Exception {
    // Table.
    session.execute("CREATE TABLE t_orderby_scan (i INT, j INT, m INT, n INT, PRIMARY KEY (i))" +
                    "  WITH TRANSACTIONS = {'enabled' : true};");

    // Indexes.
    session.execute("CREATE INDEX jdx1 ON t_orderby_scan (j, m);");
    session.execute("CREATE INDEX jdx2 ON t_orderby_scan (j, n);");

    waitForReadPermsOnAllIndexes("yugatest", "t_orderby_scan");

    // Insert data.
    session.execute("INSERT INTO t_orderby_scan(i, j, m, n) VALUES (1, 1, 1, 4);");
    session.execute("INSERT INTO t_orderby_scan(i, j, m, n) VALUES (2, 1, 2, 3);");
    session.execute("INSERT INTO t_orderby_scan(i, j, m, n) VALUES (3, 1, 3, 2);");
    session.execute("INSERT INTO t_orderby_scan(i, j, m, n) VALUES (4, 1, 4, 1);");

    // Index "jdx1" should be chosen.
    assertQuery("EXPLAIN SELECT * FROM t_orderby_scan WHERE j = 1 order by m;",
                "Row[Index Scan using yugatest.jdx1 on yugatest.t_orderby_scan]" +
                "Row[  Key Conditions: (j = 1)                                ]");

    assertQuery("SELECT * FROM t_orderby_scan WHERE j = 1 ORDER BY m LIMIT 2;",
                "Row[1, 1, 1, 4]" +
                "Row[2, 1, 2, 3]");

    // Index "jdx2" should be chosen.
    assertQuery("EXPLAIN SELECT * FROM t_orderby_scan WHERE j = 1 order by n;",
                "Row[Index Scan using yugatest.jdx2 on yugatest.t_orderby_scan]" +
                "Row[  Key Conditions: (j = 1)                                ]");

    assertQuery("SELECT * FROM t_orderby_scan WHERE j = 1 order by n LIMIT 2;",
                "Row[4, 1, 4, 1]" +
                "Row[3, 1, 3, 2]");
  }
}
