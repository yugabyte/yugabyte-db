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
import org.yb.pgsql.TestPgPortalLeak;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import static org.yb.AssertionWrappers.*;

import java.util.Map;

import com.google.common.collect.ImmutableMap;

// This module is used to verify that certain GFLAGS are setup correctly. It only sets the flag
// value, but the associated feature tests should be defined in their own file.
@RunWith(value=YBTestRunner.class)
public class TestPgFlags extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgFlags.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();

    // Flag "ysql_disable_portal_run_context" is used for "TestPgPortalLeak.java".
    flagMap.put("ysql_disable_portal_run_context", "true");

    return flagMap;
  }

  @Test
  public void testPgPortalLeakFlag() throws Exception {
    TestPgPortalLeak.testPgPortalLeakFlag();
  }
}
