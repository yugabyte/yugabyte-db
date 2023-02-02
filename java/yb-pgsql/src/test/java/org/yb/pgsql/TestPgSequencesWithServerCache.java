// Copyright (c) Yugabyte, Inc.
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

import org.junit.After;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.util.Map;

// This class tests that there are no regressions in sequence behaviour when the sequence cache is
// set to 1 but the sequence cache method is set to server.
@RunWith(value=YBTestRunner.class)
public class TestPgSequencesWithServerCache extends TestPgSequences {
  private int currentTserverId = 0;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_sequence_cache_method", "server");
    return flagMap;
  }

  @After
  public void resetCurrentTserverId() {
    currentTserverId = 0;
  }

  // Tests that needed a new cache now need a new tserver. However, we don't
  // always need to create new tservers.
  @Override
  protected Connection getConnectionWithNewCache() throws Exception {
    currentTserverId += 1;
    if (currentTserverId == miniCluster.getNumTServers()) {
      spawnTServer();
    }
    return getConnectionBuilder().withTServer(currentTserverId).connect();
  }
}
