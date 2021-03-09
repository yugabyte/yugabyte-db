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

@RunWith(value=YBTestRunner.class)
public class TestMasterLatency extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestMasterLatency.class);

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // Set latency in yb::master::CatalogManager::DeleteTable().
    builder.addMasterArgs("--catalog_manager_inject_latency_in_delete_table_ms=6000");

    // Set latency in yb::master::CatalogManager::CreateTable().
    builder.addMasterArgs("--TEST_simulate_slow_table_create_secs=6");
  }

  @Test
  public void testCreateDropTableTimeout() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());

    // Create test table.
    session.execute("create table test_table (h1 int primary key, c1 int) " +
                    "with transactions = {'enabled' : true};");
    // Drop test table.
    session.execute("drop table test_table;");

    LOG.info("End test: " + getCurrentTestMethodName());
  }
}
