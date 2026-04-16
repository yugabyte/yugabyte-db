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

import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;
import org.yb.minicluster.MiniYBClusterBuilder;

import com.google.common.collect.ImmutableMap;
/**
 * Runs the pg_regress test suite on extension queries.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressBetaExtension extends BasePgRegressTest {

  private Map<String, String> commonTserverFlags = ImmutableMap.of(
      "ysql_beta_features", "false");

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlags(commonTserverFlags);
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void testPgRegressBetaExtension() throws Exception {
    runPgRegressTest("yb_beta_extensions_schedule");
  }
}
