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

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertTrue;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestDefaultConfig extends BaseYsqlConnMgr {
  private final static String YSQL_CONN_MGR_CONFIG_FILE_NAME = "ysql_conn_mgr.conf";
  private final Map<String, String> DEFAULT_CONFIG = new HashMap<String, String>() {
    {
      put("workers", "\"auto\"");
      put("resolvers", "1");
      put("client_max", "10000");
      put("pool_discard", "no");
      // By default Ysql Connection Manager pool size is 70.
      // Pool size of global pool = Ysql Connection Manager pool size *
      // (% of server connections allocated to global pool (90)).
      // i.e. 70 * (90%).
      put("pool_size", "63");
      put("pool_reserve_prepared_statement", "yes");
    }
  };

  private Path configFile;

  @Before
  public void setPaths() throws Exception {
    configFile = Paths.get(
        miniCluster.getTabletServers()
            .get(miniCluster.getTabletServers().keySet().iterator().next()).getDataDirPath(),
        "yb-data", "tserver", YSQL_CONN_MGR_CONFIG_FILE_NAME);
  }

  private Boolean matchConfs(Map<String, String> expectedConfig,
      Map<String, String> configInFile) {
    for (String key : expectedConfig.keySet()) {
      if (!expectedConfig.get(key).equals(configInFile.get(key))) {
        LOG.error("Config value mismatch, expected %s but got %s for field %s",
            expectedConfig.get(key), configInFile.get(key), key);
        return false;
      }
    }
    return true;
  }

  // Check the config parameters present in the Ysql Connection Manager config file.
  @Test
  public void testCheckExpectedConfig() throws Exception {
    for (int i = 0; i < NUM_TSERVER; i++) {
      getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                            .withTServer(i)
                            .connect()
                            .close();
    }

    Path pathConfig = fetchConfigPath();

    {
      // Check if the config file exists.
      File file = new File(pathConfig.toString());
      assertTrue("Config file for Ysql Connection Manager is not present", file.exists());
    }

    assertTrue("Config value mismatch", matchConfs(expectedConfig(), fetchConfigs(pathConfig)));
  }

  protected Map<String, String> expectedConfig() {
    return DEFAULT_CONFIG;
  }

  private Map<String, String> fetchConfigs(Path pathConfig) {
    Map<String, String> result = new HashMap<>();
    try (BufferedReader br = new BufferedReader(new FileReader(configFile.toString()))) {
      String line;
      while ((line = br.readLine()) != null) {
        // Remove the comments.
        // Find the starting point of the comment ('#').
        // Remove all the characters after '#'.
        int indexOfComment = line.indexOf("#");
        if (indexOfComment != -1) {
          line = line.substring(0, indexOfComment);
        }

        // Check if it's begining or end of a stanza.
        if (line.contains("{") || line.contains("}")) {
          continue;
        }

        line = line.trim();

        List<String> keyValue = Arrays.asList(line.split(" "));
        if (keyValue.size() == 0) {
          continue;
        }

        String value = "";
        for (int i = 1; i < keyValue.size(); i++) {
          value = value + keyValue.get(i);
        }
        result.put(keyValue.get(0).trim(), value);
      }
    } catch (IOException e) {
      LOG.error(String.format("IOException"), e);
    }

    return result;
  }

  private Path fetchConfigPath() {
    return configFile;
  }
}
