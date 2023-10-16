// Copyright (c) YugabyteDB, Inc.
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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;

import java.util.Collections;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

import com.yugabyte.util.PSQLException;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestYbAsh extends BasePgSQLTest {
  private static final int ASH_SAMPLING_INTERVAL = 1;

  private static final int ASH_SAMPLE_SIZE = 500;

  private static final String ASH_VIEW = "yb_active_session_history";

  private void setAshConfigAndRestartCluster(
      int sampling_interval, int sample_size) throws Exception {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("TEST_yb_enable_ash", "true");
    // convert ASH_SAMPLING_INTERVAL to milliseconds
    flagMap.put("ysql_pg_conf_csv", "yb_ash_sampling_interval=" + sampling_interval * 1000 +
        ",yb_ash_sample_size=" + sample_size);
    restartClusterWithFlags(Collections.emptyMap(), flagMap);
  }

  private void executePgSleep(Statement statement, int seconds) throws Exception {
    statement.execute("SELECT pg_sleep(" + seconds + ")");
  }

  /**
   * We should get an error if we try to query the ASH view without
   * enabling ASH
   */
  @Test
  public void testAshViewWithoutEnablingAsh() throws Exception {
    try (Statement statement = connection.createStatement()) {
      runInvalidQuery(statement, "SELECT * FROM " + ASH_VIEW,
          "TEST_yb_enable_ash gflag must be enabled");
    }
  }

  /**
   * The circular buffer should be empty if the cluster is idle
   */
  @Test
  public void testEmptyCircularBuffer() throws Exception {
    setAshConfigAndRestartCluster(ASH_SAMPLING_INTERVAL, ASH_SAMPLE_SIZE);
    try (Statement statement = connection.createStatement()) {
      assertOneRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW, 0);
      Thread.sleep(2 * ASH_SAMPLING_INTERVAL * 1000); // convert to milliseconds
      assertOneRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW, 0);
    }
  }

  /**
   * Query the pg_sleep function and check if the appropriate number
   * of PgSleep wait events are present in the circular buffer
   */
  @Test
  public void testNonEmptyCircularBuffer() throws Exception {
    setAshConfigAndRestartCluster(ASH_SAMPLING_INTERVAL, ASH_SAMPLE_SIZE);
    try (Statement statement = connection.createStatement()) {
      int sleep_time = 5 * ASH_SAMPLING_INTERVAL;
      String wait_event_name = "PgSleep";
      executePgSleep(statement, sleep_time);
      // We should get atleast (sleep_time - 1) 'PgSleep' wait events, it is
      // possible that one sampling event occurs just before the sleep begins and then
      // 4 sampling events occur and one sampling event occurs after the sleep is over.
      // Type of "count" is 64-bit int, so it has to be type casted to int.
      // Since we are waiting for only a few seconds, the result should be pretty low
      // and there shouldn't be any lossy conversion
      int res = getSingleRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE wait_event='" + wait_event_name + "'").getLong(0).intValue();
      assertGreaterThanOrEqualTo(res, sleep_time - 1);
    }
  }

  /**
   * No events should be sampled if the sample size is 0
   */
  @Test
  public void testZeroSampleSize() throws Exception {
    setAshConfigAndRestartCluster(ASH_SAMPLING_INTERVAL, 0);
    try (Statement statement = connection.createStatement()) {
      int sleep_time = 2 * ASH_SAMPLING_INTERVAL;
      executePgSleep(statement, sleep_time);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW, 0);
    }
  }
}
