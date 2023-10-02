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

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_MODIFY_TABLE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_RESULT;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_VALUES_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.OPERATION_INSERT;
import static org.yb.pgsql.ExplainAnalyzeUtils.OPERATION_UPDATE;

import java.sql.Statement;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;

@RunWith(value=YBTestRunner.class)
public class TestPgExplainAnalyzeModifyTable extends BasePgExplainAnalyzeTest {
  private static final String TEST_TABLE = "abc";
  private static final String TEST_RANGE_MC_INDEX = "abc_v1_v2"; // Multi-column Range Index on abc
  private static final String TEST_HASH_INDEX = "abc_v3";
  private static final String TEST_RANGE_INDEX = "abc_v4";
  private static final long TEST_NUM_SEC_INDEXES = 3;
  private static final String CHILD_TABLE = "child";

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s (k INT PRIMARY KEY, v1 INT, "
        + "v2 INT, v3 INT, v4 INT, v5 INT) SPLIT INTO 2 TABLETS", TEST_TABLE));

      // Multi-column Range Index on (v1, v2)
      stmt.execute(String.format("CREATE INDEX %s ON %s (v1, v2 ASC)",
        TEST_RANGE_MC_INDEX, TEST_TABLE));

      // Hash Index on v3
      stmt.execute(String.format("CREATE INDEX %s ON %s (v3 HASH)",
        TEST_HASH_INDEX, TEST_TABLE));

      // Range Index on v4
      stmt.execute(String.format("CREATE INDEX %s ON %s (v4 ASC)",
        TEST_RANGE_INDEX, TEST_TABLE));

      // Create a child table for testing constraints
      stmt.execute(String.format("CREATE TABLE %s (k INT PRIMARY KEY REFERENCES %s (%s),"
        + " v INT)", CHILD_TABLE, TEST_TABLE, "k"));
    }
  }

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  @Test
  public void testInsert() throws Exception {
    final String simpleInsert = "INSERT INTO abc VALUES %s";

    TopLevelCheckerBuilder topLevelChecker = makeTopLevelBuilder()
        .storageReadRequests(Checkers.equal(0))
        .storageFlushRequests(Checkers.equal(1));

    PlanCheckerBuilder insertNodeChecker = makePlanBuilder()
        .nodeType(NODE_MODIFY_TABLE)
        .operation(OPERATION_INSERT)
        .relationName(TEST_TABLE)
        .alias(TEST_TABLE);

    // 1. Simple Insert
    PlanCheckerBuilder resultNodeChecker = makePlanBuilder()
        .nodeType(NODE_RESULT)
        .storageTableWriteRequests(Checkers.equal(1))
        .storageIndexWriteRequests(Checkers.equal(TEST_NUM_SEC_INDEXES));

    {
        Checker checker = topLevelChecker
            .plan(insertNodeChecker
                .plans(resultNodeChecker.build())
                .build())
            .build();

        testExplain(String.format(simpleInsert, "(0, 0, 0, 0, 0)"), checker);
    }

    // 2. Multiple Inserts - Single Flush
    final long numRows = 3;

    PlanCheckerBuilder valuesNodeChecker = makePlanBuilder()
        .nodeType(NODE_VALUES_SCAN)
        .storageTableWriteRequests(Checkers.equal(numRows))
        .storageIndexWriteRequests(Checkers.equal(TEST_NUM_SEC_INDEXES * numRows));

    {
        Checker checker = topLevelChecker
            .plan(insertNodeChecker
                .plans(valuesNodeChecker.build())
                .build())
            .build();

        testExplain(String.format(simpleInsert, "(1, 1, 1, 1, 1), (2, 2, 2, 2, 2)"
        + ", (3, 3, 3, 3, 3)"), checker);
    }

  }

  @Test
  public void testUpdate() throws Exception {
    final String updateQuery = "UPDATE %s SET %s = %s + 5 WHERE %s %s";
    final int numRows = 4;

    // Populate the table with some rows.
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("TRUNCATE %s CASCADE", TEST_TABLE));
      stmt.execute(String.format("INSERT INTO %s VALUES %s", TEST_TABLE,
        "(0, 0, 0, 0, 0), (1, 1, 1, 1, 1), (2, 2, 2, 2, 2), (3, 3, 3, 3, 3)"));
    }

    TopLevelCheckerBuilder topLevelChecker = makeTopLevelBuilder();

    PlanCheckerBuilder updateNodeChecker = makePlanBuilder()
        .nodeType(NODE_MODIFY_TABLE)
        .operation(OPERATION_UPDATE)
        .relationName(TEST_TABLE)
        .alias(TEST_TABLE);

    // 1. Update primary key column using primary key.
    PlanCheckerBuilder indexScanChecker = makePlanBuilder()
        .nodeType(NODE_INDEX_SCAN)
        .relationName(TEST_TABLE)
        .alias(TEST_TABLE)
        .storageTableReadRequests(Checkers.equal(1))
        .storageTableReadExecutionTime(Checkers.greater(0.0))
        .storageTableWriteRequests(Checkers.equal(2))
        .storageIndexWriteRequests(Checkers.equal(TEST_NUM_SEC_INDEXES * 2));

    {
        Checker checker = topLevelChecker
            .plan(updateNodeChecker
                .plans(indexScanChecker.build())
                .build())
            .build();

        testExplain(String.format(updateQuery, TEST_TABLE, "k", "k", "k", "= 0"), checker);
    }


    // 2. Update multiple rows of primary key column using primary key.
    {
        PlanCheckerBuilder seqScanChecker = makePlanBuilder()
            .nodeType(NODE_SEQ_SCAN)
            .relationName(TEST_TABLE)
            .alias(TEST_TABLE)
            .storageTableReadRequests(Checkers.equal(1))
            .storageTableReadExecutionTime(Checkers.greater(0.0))
            .storageTableWriteRequests(Checkers.equal(numRows * 2))
            .storageIndexWriteRequests(Checkers.equal(numRows * TEST_NUM_SEC_INDEXES * 2));

        Checker checker = topLevelChecker
            .plan(updateNodeChecker
                .plans(seqScanChecker.build())
                .build())
            .storageFlushRequests(Checkers.equal(1))
            .storageWriteRequests(Checkers.equal(numRows * 2 * (TEST_NUM_SEC_INDEXES + 1)))
            .storageReadRequests(Checkers.equal(numRows * 2 + 1))
            .build();

        testExplain(String.format(updateQuery, TEST_TABLE, "k", "k", "k", "> 0"), checker);
    }


    // 3. Update multiple rows of secondary index using primary key.
    {
        PlanCheckerBuilder seqScanChecker = makePlanBuilder()
            .nodeType(NODE_SEQ_SCAN)
            .relationName(TEST_TABLE)
            .alias(TEST_TABLE)
            .storageTableReadRequests(Checkers.equal(1))
            .storageTableReadExecutionTime(Checkers.greater(0.0))
            .storageTableWriteRequests(Checkers.equal(numRows))
            .storageIndexWriteRequests(Checkers.equal(numRows * 2));

        Checker checker = topLevelChecker
            .plan(updateNodeChecker
                .plans(seqScanChecker.build())
                .build())
            .storageFlushRequests(Checkers.equal(1))
            .storageWriteRequests(Checkers.equal(numRows * 3))
            .storageReadRequests(Checkers.equal(1))
            .build();

        testExplain(String.format(updateQuery, TEST_TABLE, "v4", "v4", "k", "> 0"), checker);
    }
  }

  @Test
  public void testOnConflict() throws Exception {
    final String insertWithOnConflict = "INSERT INTO %s VALUES %s ON CONFLICT (%s) %s %s";

    // Clear the table.
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("TRUNCATE %s CASCADE", TEST_TABLE));
    }

    TopLevelCheckerBuilder topLevelChecker = makeTopLevelBuilder()
        .storageReadRequests(Checkers.greater(0))
        .catalogWriteRequests(Checkers.equal(0));

    PlanCheckerBuilder resultNodeChecker = makePlanBuilder()
        .nodeType(NODE_RESULT);

    PlanCheckerBuilder insertNodeChecker = makePlanBuilder()
        .nodeType(NODE_MODIFY_TABLE)
        .operation(OPERATION_INSERT)
        .relationName(TEST_TABLE)
        .alias(TEST_TABLE);

    // 1. ON CONFLICT DO NOTHING - No Conflict
    {
        Checker checker = topLevelChecker
            .plan(insertNodeChecker
                .plans(resultNodeChecker
                    .storageTableReadRequests(Checkers.equal(1))
                    .storageTableWriteRequests(Checkers.equal(1))
                    .storageIndexWriteRequests(Checkers.equal(TEST_NUM_SEC_INDEXES))
                    .build())
                .build())
            .storageFlushRequests(Checkers.equal(1))
            .storageReadRequests(Checkers.equal(1))
            .storageWriteRequests(Checkers.equal(1 + TEST_NUM_SEC_INDEXES))
            .build();

        testExplain(String.format(insertWithOnConflict, TEST_TABLE, "(0, 0, 0, 0, 0)", "k",
        "DO NOTHING", ""), checker);
    }

    // 2. ON CONFLICT DO NOTHING - Conflict
    {
        Checker checker = topLevelChecker
            .plan(insertNodeChecker
                .plans(makePlanBuilder()
                    .nodeType(NODE_RESULT)
                    .storageTableReadRequests(Checkers.equal(1))
                    .build())
                .build())
            .storageFlushRequests(Checkers.equal(0))
            .storageReadRequests(Checkers.equal(1))
            .storageWriteRequests(Checkers.equal(0))
            .build();

        testExplain(String.format(insertWithOnConflict, TEST_TABLE, "(0, 0, 0, 0, 0)", "k",
        "DO NOTHING", ""), checker);
    }

    // 3. ON CONFLICT DO UPDATE - No conflict
    {
        Checker checker = topLevelChecker
            .plan(insertNodeChecker
                .plans(resultNodeChecker
                    .storageTableReadRequests(Checkers.equal(1))
                    .storageTableWriteRequests(Checkers.equal(1))
                    .storageIndexWriteRequests(Checkers.equal(TEST_NUM_SEC_INDEXES))
                    .build())
                .build())
            .storageFlushRequests(Checkers.equal(1))
            .storageReadRequests(Checkers.equal(1))
            .storageWriteRequests(Checkers.equal(1 + TEST_NUM_SEC_INDEXES))
            .build();

        testExplain(String.format(insertWithOnConflict, TEST_TABLE, "(1, 1, 1, 1, 1)", "k",
        "DO UPDATE", "SET v5 = abc.v5 + 1"), checker);

    }

    // 4. ON CONFLICT DO UPDATE - Conflict
    {
        Checker checker = topLevelChecker
            .plan(insertNodeChecker
                .plans(resultNodeChecker
                    .storageTableReadRequests(Checkers.equal(1))
                    .storageTableWriteRequests(Checkers.equal(1))
                    .storageIndexWriteRequests(Checkers.equal(TEST_NUM_SEC_INDEXES * 2))
                    .build())
                .build())
            .storageFlushRequests(Checkers.equal(2))
            .storageReadRequests(Checkers.equal(1))
            .storageWriteRequests(Checkers.equal((TEST_NUM_SEC_INDEXES * 2) + 1))
            .build();

        testExplain(String.format(insertWithOnConflict, TEST_TABLE, "(1, 1, 1, 1, 1)", "k",
        "DO UPDATE", "SET v5 = abc.v5 + 1"), checker);
    }
  }

  @Test
  public void testForeignKey() throws Exception {
    final String fkSimpleInsert = "INSERT INTO %s VALUES %s";

    // Create a table with a foreign key constraint.
    // Populate the table with some rows.
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("TRUNCATE %s CASCADE", TEST_TABLE));
      stmt.execute(String.format("INSERT INTO %s VALUES %s", TEST_TABLE,
        "(0, 0, 0, 0, 0), (1, 1, 1, 1, 1), (2, 2, 2, 2, 2), (3, 3, 3, 3, 3)"));
    }

    TopLevelCheckerBuilder topLevelChecker = makeTopLevelBuilder()
        .storageReadRequests(Checkers.equal(1))
        .storageFlushRequests(Checkers.equal(1))
        .catalogWriteRequests(Checkers.equal(0));

    PlanCheckerBuilder insertNodeChecker = makePlanBuilder()
        .nodeType(NODE_MODIFY_TABLE)
        .operation(OPERATION_INSERT)
        .relationName(CHILD_TABLE)
        .alias(CHILD_TABLE);

    // 1. Simple Insert
    PlanCheckerBuilder resultNodeChecker = makePlanBuilder()
        .nodeType(NODE_RESULT)
        .storageTableWriteRequests(Checkers.equal(1));

    Checker checker = topLevelChecker
        .plan(insertNodeChecker
            .plans(resultNodeChecker.build())
            .build())
        .build();

    testExplain(String.format(fkSimpleInsert, CHILD_TABLE, "(0, 0)"), checker);
  }
}
