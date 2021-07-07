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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.RocksDBMetrics;
import org.yb.util.YBTestRunnerNonTsanOnly;
import static org.yb.AssertionWrappers.*;

import java.sql.Statement;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgDdl extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgDdl.class);

  @Test
  public void testLargeTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      for (int i = 0; i < 111; ++i) {
        // Table is created with a single tablet for performance reason.
        stmt.execute(String.format("CREATE TABLE table_%d (k INT) SPLIT INTO 1 TABLETS", i + 1));
      }
      stmt.execute("COMMIT");
    }
  }

  @Test
  public void testCreateTableWithPrimaryKey() throws Exception {
    LOG.info("TEST YSQL CREATE TABLE - Start");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE has_primary_key(k INT PRIMARY KEY)");
      stmt.execute("CREATE TABLE no_primary_key(k INT)");
      RocksDBMetrics hasPrimaryKeyRocksDBMetrics = getRocksDBMetric("has_primary_key");
      RocksDBMetrics noPrimaryKeyRocksDBMetrics = getRocksDBMetric("no_primary_key");
      // Primary key Index is an implicit part of the base table in Yugabyte
      // and doesn't need to be built explicitly. Number of RocksDB seek() and next() calls
      // should be same for creating tables with and without primary key.
      assertEquals(noPrimaryKeyRocksDBMetrics.seekCount, hasPrimaryKeyRocksDBMetrics.seekCount);
      assertEquals(noPrimaryKeyRocksDBMetrics.nextCount, hasPrimaryKeyRocksDBMetrics.nextCount);
      LOG.info("has_primary_key table seek count is: {}. no_primary_key table seek count is: {}.",
               hasPrimaryKeyRocksDBMetrics.seekCount, noPrimaryKeyRocksDBMetrics.seekCount);
      LOG.info("has_primary_key table next count is: {}. no_primary_key table next count is: {}.",
               hasPrimaryKeyRocksDBMetrics.nextCount, noPrimaryKeyRocksDBMetrics.nextCount);

      // Test with different number of partitions
      stmt.execute("CREATE TABLE has_primary_key_10_tablets(k INT PRIMARY KEY)"
                   + "SPLIT INTO 10 TABLETS");
      stmt.execute("CREATE TABLE no_primary_key_10_tablets(k INT) SPLIT INTO 10 TABLETS");
      RocksDBMetrics hasPrimaryKeyRocksDBMetrics2 = getRocksDBMetric("has_primary_key_10_tablets");
      RocksDBMetrics noPrimaryKeyRocksDBMetrics2 = getRocksDBMetric("no_primary_key_10_tablets");
      assertEquals(noPrimaryKeyRocksDBMetrics2.seekCount, hasPrimaryKeyRocksDBMetrics2.seekCount);
      assertEquals(noPrimaryKeyRocksDBMetrics2.nextCount, hasPrimaryKeyRocksDBMetrics2.nextCount);
      LOG.info("has_primary_key_10_tablets table seek count is: {}. no_primary_key_10_tablets table"
               + "seek count is: {}.", hasPrimaryKeyRocksDBMetrics2.seekCount,
               noPrimaryKeyRocksDBMetrics2.seekCount);
      LOG.info("has_primary_key_10_tablets table next count is: {}. no_primary_key_10_tablets table"
               + "next count is: {}.", hasPrimaryKeyRocksDBMetrics2.nextCount,
               noPrimaryKeyRocksDBMetrics2.nextCount);
    }
    LOG.info("TEST YSQL CREATE TABLE - End");
  }
}
