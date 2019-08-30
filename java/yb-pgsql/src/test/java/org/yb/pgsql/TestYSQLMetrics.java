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

import org.yb.minicluster.MiniYBDaemon;


import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.net.MalformedURLException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestYSQLMetrics extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(MiniYBDaemon.class);

  @Test
  public void testMetrics() throws Exception {
    Statement statement = connection.createStatement();

    // DDL is non-txn.
    verifyStatementMetric(statement, "CREATE TABLE test (k int PRIMARY KEY, v int)",
                          OTHER_STMT_METRIC, 1, 0, true);

    // Select uses txn.
    verifyStatementMetric(statement, "SELECT * FROM test",
                          SELECT_STMT_METRIC, 1, 1, true);

    // Non-txn insert.
    verifyStatementMetric(statement, "INSERT INTO test VALUES (1, 1)",
                          INSERT_STMT_METRIC, 1, 0, true);
    // Txn insert.
    statement.execute("BEGIN");
    verifyStatementMetric(statement, "INSERT INTO test VALUES (2, 2)",
                          INSERT_STMT_METRIC, 1, 1, true);
    statement.execute("END");

    // Non-txn update.
    verifyStatementMetric(statement, "UPDATE test SET v = 2 WHERE k = 1",
                          UPDATE_STMT_METRIC, 1, 0, true);
    // Txn update.
    verifyStatementMetric(statement, "UPDATE test SET v = 3",
                          UPDATE_STMT_METRIC, 1, 1, true);

    // Non-txn delete.
    verifyStatementMetric(statement, "DELETE FROM test WHERE k = 2",
                          DELETE_STMT_METRIC, 1, 0, true);
    // Txn delete.
    verifyStatementMetric(statement, "DELETE FROM test",
                          DELETE_STMT_METRIC, 1, 1, true);

    // Invalid statement should not update metrics.
    verifyStatementMetric(statement, "INSERT INTO invalid_table VALUES (1)",
                          INSERT_STMT_METRIC, 0, 0, false);
  }
}
