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
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Statement;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgDdl extends BasePgSQLTest {

  @Test
  public void testLargeTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      for (int i = 0; i < 30; ++i) {
        stmt.execute(String.format("CREATE TABLE table_%d (k INT PRIMARY KEY, " +
            "v_1 INT, v_2 INT, v_3 INT, v_4 INT, v_5 INT, " +
            "v_6 INT, v_7 INT, v_8 INT, v_9 INT, v_10 INT)", i + 1));
        for (int j = 0; j < 10; ++j) {
          stmt.execute(String.format("CREATE INDEX ON table_%d(v_%d)", i + 1, j + 1));
        }
      }
      stmt.execute("COMMIT");
    }
  }
}
