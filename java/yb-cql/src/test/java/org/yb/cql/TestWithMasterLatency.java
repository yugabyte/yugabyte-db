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

import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestWithMasterLatency extends BaseCQLTest {

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // Set latency in yb::master::CatalogManager::DeleteTable().
    builder.addMasterArgs("--catalog_manager_inject_latency_in_delete_table_ms=6000");
  }

  @Test
  public void testDropTableTimeout() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());

    // Create test table.
    session.execute("create table test_drop (h1 int primary key, " +
                    "c1 int, c2 int, c3 int, c4 int, c5 int) " +
                    "with transactions = {'enabled' : true};");
    // Drop test table.
    session.execute("drop table test_drop;");

    LOG.info("End test: " + getCurrentTestMethodName());
  }
}
