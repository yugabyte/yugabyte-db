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

import org.yb.util.YBTestRunnerNonTsanAsan;

@RunWith(value=YBTestRunnerNonTsanAsan.class)
public class TestBigNumShards extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestBigNumShards.class);

  @Override
  protected int getNumShardsPerTServer() {
    return 32;
  }

  @Test
  public void testCreateDropTable() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());

    // Create test table.
    session.execute("create table test_drop (h1 int primary key, " +
                    "c1 int, c2 int, c3 int, c4 int, c5 int) " +
                    "with transactions = {'enabled' : true};");

    // Create test index.
    session.execute("create index i1 on test_drop (c1);");

    // Drop test table.
    session.execute("drop table test_drop;");
  }
}
