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

import java.sql.SQLException;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

@RunWith(YBTestRunner.class)
public class TestToastFunction extends TestToastFunctionBase {
  private static final Logger LOG = LoggerFactory.getLogger(TestToastFunction.class);

  @Test
  public void testSimple() throws SQLException {
    setEnableToastFlag(true);
    CacheMemoryContextTracker cxt = simpleTest();
    cxt.assertMemoryUsageLessThan(300 * KB);
  }

  @Test
  public void testCatalogRefreshMemoryUsage() throws Exception {
    setEnableToastFlag(true);
    List<CacheMemoryContextTracker> contexts = catalogRefreshMemoryUsageTest();
    contexts.forEach(c -> c.assertMemoryUsageLessThan(7 * MB));
  }

  @Test
  public void testAdHocMemoryUsage() throws Exception {
    setEnableToastFlag(true);
    List<CacheMemoryContextTracker> contexts = adHocMemoryUsageTest();
    contexts.forEach(c -> c.assertMemoryUsageLessThan(7 * MB));
  }

  @Test
  public void testBuildRelcacheInitFileMemoryUsage() throws Exception {
    setEnableToastFlag(true);
    CacheMemoryContextTracker cxt = buildRelcacheInitFileMemoryUsage();
    cxt.assertMemoryUsageLessThan(8 * MB);
  }
}
