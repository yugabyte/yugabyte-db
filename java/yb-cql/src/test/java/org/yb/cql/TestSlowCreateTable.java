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
package org.yb.cql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

@RunWith(value=YBTestRunner.class)
public class TestSlowCreateTable extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestSlowCreateTable.class);

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // Set latency in yb::master::CatalogManager::CreateTable().
    builder.addMasterFlag("TEST_simulate_slow_table_create_secs", "20");
    // Set default CREATE/DROP TABLE operation timeout.
    builder.addCommonTServerFlag("yb_client_admin_operation_timeout_sec", "10");
  }

  @Test
  public void testCreateTableTimeout() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());

    // Create test table.
    try {
      session.execute("create table test_table (h1 int primary key, c1 int) " +
                      "with transactions = {'enabled' : true};");
      fail("Create Table did not fail due to timeout as expected");
    } catch (com.datastax.driver.core.exceptions.ServerError ex) {
      LOG.info("Expected exception:", ex);
      String errorMsg = ex.getCause().getMessage();
      if (!errorMsg.contains("Timed out waiting for Table Creation") &&
          !errorMsg.contains("CreateTable timed out after deadline expired, passed")) {
        fail("Unexpected error message '" + errorMsg + "'");
      }
    }

    Thread.sleep(15 * 1000); // Let CREATE TABLE finish.

    // Drop test table.
    session.execute("drop table test_table;");
  }
}
