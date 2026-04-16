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

// TODO (janand) #18837: Use Parameterized test runner with pool size as the parameter.
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestUserContextLimitedPoolSize extends TestUserContext {
  // TODO: Revert to 2 connections after bug fix DB-7395 lands.
  private final int POOL_SIZE = 3;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_max_conns_per_db", Integer.toString(POOL_SIZE));
      }
    };

    builder.addCommonTServerFlags(additionalTserverFlags);
  }

}
