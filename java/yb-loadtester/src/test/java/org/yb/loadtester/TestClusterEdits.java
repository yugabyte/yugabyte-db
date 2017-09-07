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

import com.google.common.net.HostAndPort;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.yugabyte.sample.Main;
import com.yugabyte.sample.common.CmdLineOpts;
import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common;
import org.yb.client.ChangeConfigResponse;
import org.yb.minicluster.MiniYBCluster;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ModifyMasterClusterConfigBlacklist;
import org.yb.client.ModifyClusterConfigReplicationFactor;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.cql.BaseCQLTest;
import org.yb.minicluster.MiniYBDaemon;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.Socket;
import java.net.URL;
import java.net.URLConnection;
import java.util.*;

import static junit.framework.TestCase.assertFalse;
import static junit.framework.TestCase.assertNotNull;
import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

/**
 * This is an integration test that ensures we can expand, shrink and fully move a YB cluster
 * without any significant impact to a running load test.
 */
public class TestClusterEdits extends TestClusterBase {

  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 10 minutes.
  public void testClusterFullMove() throws Exception {
    // Wait for load tester to generate traffic.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);

    // First try to move all the masters.
    performFullMasterMove();

    // Wait for some ops and verify no failures in load tester.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);
    loadTesterRunnable.verifyNumExceptions();

    // Now perform a full tserver move.
    performTServerExpandShrink(true);

    verifyClusterHealth();
  }

  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 10 minutes.
  public void testClusterExpandAndShrink() throws Exception {
    // Wait for load tester to generate traffic.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);

    // Now perform a tserver expand and shrink.
    performTServerExpandShrink(false);

    verifyClusterHealth();
  }

  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 10 minutes.
  public void testRF3toRF5() throws Exception {
    performRFChange(3, 5);
  }
}
