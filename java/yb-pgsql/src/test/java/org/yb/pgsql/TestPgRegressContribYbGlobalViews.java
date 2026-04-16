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

import org.apache.commons.io.FileUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.YBTestRunner;

import java.io.File;
import java.nio.file.Paths;
import java.util.Map;

/**
 * Regression tests for global views using file_fdw as a per-node data source.
 *
 * Each tserver gets a different CSV file placed in its pg_data directory.
 * The SQL test uses file_fdw to read local data and postgres_fdw with
 * server_type 'federatedYugabyteDB' to aggregate across all nodes.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressContribYbGlobalViews extends BasePgRegressTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgRegressContribYbGlobalViews.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "yb_enable_global_views=true");
    return flagMap;
  }

  /**
   * Copy per-node CSV files into each tserver's pg_data directory so that
   * file_fdw reads different data on each node (simulating per-node views
   * like pg_stat_statements).
   */
  private void copyDataFilesToTservers() throws Exception {
    File srcDir = TestUtils.getClassResourceDir(getClass());
    int nodeIdx = 1;
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      File srcFile = new File(srcDir, "gv_node" + nodeIdx + ".csv");
      File destFile = Paths.get(ts.getDataDirPath(), "pg_data", "gv_test_data.csv").toFile();
      LOG.info("Copying {} to {}", srcFile, destFile);
      FileUtils.copyFile(srcFile, destFile);
      nodeIdx++;
    }
  }

  @Test
  public void schedule() throws Exception {
    copyDataFilesToTservers();
    runPgRegressTest(new File(TestUtils.getBuildRootDir(), "postgres_build/contrib/postgres_fdw"),
                     "yb_gv_schedule");
  }
}
