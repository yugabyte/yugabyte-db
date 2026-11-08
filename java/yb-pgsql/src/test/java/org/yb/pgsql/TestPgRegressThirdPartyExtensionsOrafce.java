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

import static org.junit.Assume.assumeFalse;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;

import java.io.File;
import java.util.Collections;
import java.util.Map;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressThirdPartyExtensionsOrafce extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  // (Auto-Analyze #28391) Restarting a DDL transaction not supported
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_enable_auto_analyze", "false");
    return flagMap;
  }

  // utl_file.tmpdir() reads TMPDIR (default /tmp); use a per-test dir so that concurrent runs of
  // the files test don't clobber each other's regress_orafce.txt.
  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addEnvironmentVariables(
        Collections.singletonMap("TMPDIR", TestUtils.getBaseTmpDir()));
  }

  @Test
  @BypassConnMgr(reason = BasePgSQLTest.EXTENSION_NOT_SUPPORTED)
  public void schedule() throws Exception {
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/third-party-extensions/orafce"),
                     "yb_schedule");
  }
}
