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

import java.util.HashMap;
import java.util.Map;

import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;

// Check that values from Gflags gets reflected in the Ysql Connection Manager config file.
// TODO (janand) #18837: Use Parameterized test runner with TSERVER_FLAGS and EXPECTED_CONFIG_VALUES
// as the parameter.
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestGFlags extends TestDefaultConfig {

  private final Map<String, String> TSERVER_FLAGS = new HashMap() {
    {
      put("ysql_conn_mgr_max_client_connections", "1000");
      put("ysql_conn_mgr_num_workers", "8");
      put("ysql_conn_mgr_max_conns_per_db", "22");
    }
  };

  private final Map<String, String> EXPECTED_CONFIG_VALUES = new HashMap() {
    {
      put("client_max", "1000");
      put("pool_size", "22");
      put("workers", "8");
    }
  };

  // Add additional gflags for configuring the Ysql Connection Manager.
  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    builder.addCommonTServerFlags(TSERVER_FLAGS);
    super.customizeMiniClusterBuilder(builder);
  }

  @Override
  protected Map<String, String> expectedConfig() {
    return EXPECTED_CONFIG_VALUES;
  }
}
