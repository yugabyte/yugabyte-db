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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;

// In this test module we adjust the number of rows to be prefetched by PgGate and make sure that
// the result for the query are correct.
@RunWith(value=YBTestRunner.class)
public class TestPgIndexPagingState extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgIndexPagingState.class);

  @Override
  protected Integer getYsqlPrefetchLimit() {
    // Set the prefetch limit to 2 to create the test scenarios where we got the paging state for
    // an index table of a sys catalog.
    //
    // See github #1865.
    return 2;
  }

  @Test
  public void testSmallPrefetchLimit() throws SQLException {
    // Create a dummy table to make sure that the connected database is operating normally.
    createSimpleTable("test_with_small_prefetch_limit");
  }
}
