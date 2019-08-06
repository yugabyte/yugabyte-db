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

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.pgsql.PgRegressRunner;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.lang.ProcessBuilder;
import java.sql.*;
import java.util.*;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgDump extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgDump.class);

  @Test
  public void testPgDump() throws Exception {
    // Location of Postgres regression tests
    File pgRegressDir = PgRegressRunner.getPgRegressDir();

    // Create the data
    BufferedReader inputIn = new BufferedReader(
                                           new FileReader(
                                               new File(pgRegressDir,
                                                        "sql/yb_pg_dump.sql")));
    try (Statement statement = connection.createStatement()) {
      String inputLine = null;
      while ((inputLine = inputIn.readLine()) != null) {
        LOG.info(inputLine);
        statement.execute(inputLine);
        LOG.info("Executed");
      }
    }

    // Dump and validate the data
    File pgBinDir = PgRegressRunner.getPgBinDir();
    File pgDumpExec = new File(pgBinDir, "pg_dump");

    final int tserverIndex = 0;
    File actual = new File(pgRegressDir, "output/yb_pg_dump.out");
    ProcessBuilder pb = new ProcessBuilder(pgDumpExec.toString(), "-h", getPgHost(tserverIndex),
                                           "-p", Integer.toString(getPgPort(tserverIndex)),
                                           "-U", DEFAULT_PG_USER,
                                           "-f", actual.toString());
    pb.start().waitFor();

    BufferedReader actualIn = new BufferedReader(new FileReader(actual));
    BufferedReader expectedIn = new BufferedReader(
                                        new FileReader(
                                            new File(pgRegressDir,
                                                     "expected/yb_pg_dump.out")));
    String actualLine = null, expectedLine = null;
    while ((actualLine = actualIn.readLine()) != null) {
      expectedLine = expectedIn.readLine();
      assertEquals(actualLine, expectedLine);
    }
    assertEquals(expectedIn.readLine(), null);
  }
}
