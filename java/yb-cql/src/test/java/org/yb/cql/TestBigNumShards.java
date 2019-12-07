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

import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestBigNumShards extends BaseCQLTest {

  @Override
  protected int overridableNumShardsPerTServer() {
    return 32;
  }

  @Test
  public void testDropTableTimeout() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());

    // Create test table.
    session.execute("create table test_drop (h1 int primary key, " +
                    "c1 int, c2 int, c3 int, c4 int, c5 int) " +
                    "with transactions = {'enabled' : true};");

    // Create test indexes.
    session.execute("create index i1 on test_drop (c1);");
    session.execute("create index i2 on test_drop (c2);");
    session.execute("create index i3 on test_drop (c3);");
    session.execute("create index i4 on test_drop (c4);");
    session.execute("create index i5 on test_drop (c5);");

    // Drop test table.
    session.execute("drop table test_drop;");

    LOG.info("End test: " + getCurrentTestMethodName());
  }
}
