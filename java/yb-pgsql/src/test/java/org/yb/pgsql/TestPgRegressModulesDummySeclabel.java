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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.BuildTypeUtil;
import org.yb.YBTestRunner;
import java.io.File;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressModulesDummySeclabel extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return (int) BuildTypeUtil.adjustTimeout(300);
  }

  @Test
  public void schedule() throws Exception {
    // (abhinab-yb) GH #26650: Dummy security labels are not getting loaded.
    // TODO: Enable the test with connection manager once above issue is fixed.
    skipYsqlConnMgr(BasePgSQLTest.DUMMY_LABEL_NOT_LOADED_ON_ALL_BACKENDS,
                    isTestRunningWithConnectionManager());
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/src/test/modules/dummy_seclabel"),
                     "yb_schedule");
  }
}
