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

import java.util.Collections;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import com.google.common.collect.ImmutableMap;

// Runs the pg_regress test suite on YB code.
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressTablegroup extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgRegressTablegroup.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_beta_feature_tablegroup", "true");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void testPgRegressTablegroup() throws Exception {
    runPgRegressTest("yb_tablegroups_schedule");
  }

  @Test
  public void testPgRegressTablegroupDeprecation() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithFlags(Collections.emptyMap(),
                            ImmutableMap.of(
                                "ysql_beta_features", "false",
                                "ysql_beta_feature_tablegroup", "false"));

    runPgRegressTest("yb_tablegroup_deprecation_schedule");
  }
}
