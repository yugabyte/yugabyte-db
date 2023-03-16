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
package org.yb.loadtester;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

/**
 * This is an integration test that ensures we can fully move a YB cluster
 * without any significant impact to a running load test.
 */

@RunWith(value=YBTestRunner.class)
public class TestClusterFullMove extends TestClusterBase {
  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 20 minutes.
  public void testClusterFullMove() throws Exception {
    // Wait for load tester to generate traffic.
    loadTesterRunnable.waitNumOpsIncrement(NUM_OPS_INCREMENT);

    // First try to move all the masters.
    performFullMasterMove();

    // Wait for some ops and verify no failures in load tester.
    loadTesterRunnable.waitNumOpsIncrement(NUM_OPS_INCREMENT);
    loadTesterRunnable.verifyNumExceptions();

    // Now perform a full tserver move.
    performTServerExpandShrink(true);

    verifyClusterHealth();
  }
}
