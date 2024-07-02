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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;

import java.io.File;

import java.util.Map;

@RunWith(value = YBTestRunner.class)
public class TestPgRegressContribPasswordCheck extends BasePgRegressTest {
    @Override
    public int getTestMethodTimeoutSec() {
        return 1800;
    }

    @Override
    protected Map<String, String> getTServerFlags() {
        Map<String, String> flagMap = super.getTServerFlags();
        appendToYsqlPgConf(flagMap, "shared_preload_libraries='passwordcheck'");
        return flagMap;
    }

    @Test
    public void schedule() throws Exception {
        runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                "postgres_build/contrib/passwordcheck"),
                "yb_schedule");
    }
}
