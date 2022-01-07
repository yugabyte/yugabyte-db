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
import org.yb.minicluster.LogPrinter;
import org.yb.pgsql.PgRegressRunner;
import org.yb.util.RandomNumberUtil;
import org.yb.util.SideBySideDiff;
import org.yb.util.StringUtil;
import org.yb.util.YBTestRunnerNonTsanAsan;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.lang.ProcessBuilder;
import java.sql.*;
import java.util.*;
import java.util.regex.Pattern;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanAsan.class)
public class TestYsqlDump extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYsqlDump.class);

  private static final int TURN_OFF_SEQUENCE_CACHE_FLAG = 0;

  private LogPrinter stdoutLogPrinter, stderrLogPrinter;

  @Override
  public int getTestMethodTimeoutSec() {
    return super.getTestMethodTimeoutSec() * 10;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_sequence_cache_minval", Integer.toString(TURN_OFF_SEQUENCE_CACHE_FLAG));
    return flagMap;
  }

  // The following logic is needed to remove the dependency on the exact version number from
  // the ysql_dump output part that looks like this:
  // -- Dumped from database version 11.2-YB-1.3.2.0-b0
  // -- Dumped by ysql_dump version 11.2-YB-1.3.2.0-b0

  private static String VERSION_STR_PREFIX = " version ";
  private static Pattern VERSION_NUMBER_PATTERN = Pattern.compile(
      VERSION_STR_PREFIX + "[0-9]+[.][0-9]+-YB-([0-9]+[.]){3}[0-9]+-b[0-9]+");
  private static String VERSION_NUMBER_REPLACEMENT_STR =
      VERSION_STR_PREFIX + "X.X-YB-X.X.X.X-bX";

  private String postprocessOutputLine(String s) {
    if (s == null)
      return null;
    return StringUtil.rtrim(
      VERSION_NUMBER_PATTERN.matcher(s).replaceAll(VERSION_NUMBER_REPLACEMENT_STR));
  }

  private static void expectOnlyEmptyLines(String message,
                                           String curLine,
                                           BufferedReader in) throws IOException {
    while (curLine != null) {
      assertEquals(message, "", curLine.trim());
      curLine = in.readLine();
    }
  }

  @Test
  public void testPgDump() throws Exception {
    testPgDumpHelper("ysql_dump" /* binaryName */,
                     "sql/yb_ysql_dump.sql" /* inputFileRelativePath */,
                     "output/yb_ysql_dump.out" /* outputFileRelativePath */,
                     "expected/yb_ysql_dump.out" /* expectedFileRelativePath */,
                     true /* includeYbMetadata */);
  }

  @Test
  public void testPgDumpAll() throws Exception {
    testPgDumpHelper("ysql_dumpall" /* binaryName */,
                     "sql/yb_ysql_dumpall.sql" /* inputFileRelativePath */,
                     "output/yb_ysql_dumpall.out" /* outputFileRelativePath */,
                     "expected/yb_ysql_dumpall.out" /* expectedFileRelativePath */,
                     false /* includeYbMetadata is not an option for dumpall */);
  }

  @Test
  public void testPgDumpVerifyOutput() throws Exception {
    /*
     * To verify that dumps can be loaded properly.
     * First, it restores the database using the (pre-generated) dump files created by dump and
     * dumpall.
     * Then runs the contents of yb_ysql_dump_verifier.sql and ensures that everything
     * matches what was expected.
     */
    restartCluster(); // create a new cluster for this test
    markClusterNeedsRecreation(); // create a new cluster for the next test

    File pgRegressDir = PgRegressBuilder.getPgRegressDir();

    final int tserverIndex = 0;
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlshExec = new File(pgBinDir, "ysqlsh");
    File dumpOutput = new File(pgRegressDir, "expected/yb_ysql_dump.out");
    File dumpallOutput = new File(pgRegressDir, "expected/yb_ysql_dumpall.out");
    File input = new File(pgRegressDir, "sql/yb_ysql_dump_verifier.sql");
    File actual = new File(pgRegressDir, "output/yb_ysql_dump_verifier.out");
    File expected = new File(pgRegressDir, "expected/yb_ysql_dump_verifier.out");

    // Create some data before loading the dumps
    try (Statement statement = connection.createStatement()) {
      // These users are required by the output from pg_dump
      statement.execute("CREATE USER regress_rls_alice NOLOGIN");
      statement.execute("CREATE USER rls_user NOLOGIN");
      statement.execute("CREATE USER tablegroup_test_user SUPERUSER");
      // These tables are created to shift OIDs by a few so we can be more sure that dumps aren't
      // depending on fragile OIDs
      statement.execute("CREATE TABLE this_table_is_just_to_shift_oids_by_1 (a INT)");
      statement.execute("CREATE TABLE this_table_is_just_to_shift_oids_by_2 (a INT)");
      statement.execute("CREATE TABLE this_table_is_just_to_shift_oids_by_3 (a INT)");
    }

    buildAndRunProcess("ysqlsh_load_dumpall", Arrays.asList(
      ysqlshExec.toString(),
      "-h", getPgHost(tserverIndex),
      "-p", Integer.toString(getPgPort(tserverIndex)),
      "-U", DEFAULT_PG_USER,
      "-f", dumpallOutput.toString()
    ));

    buildAndRunProcess("ysqlsh_load_dump", Arrays.asList(
      ysqlshExec.toString(),
      "-h", getPgHost(tserverIndex),
      "-p", Integer.toString(getPgPort(tserverIndex)),
      "-U", DEFAULT_PG_USER,
      "-f", dumpOutput.toString()
    ));

    // Run some validations
    int exitCode = buildAndRunProcess("ysqlsh", Arrays.asList(
      ysqlshExec.toString(),
      "-h", getPgHost(tserverIndex),
      "-p", Integer.toString(getPgPort(tserverIndex)),
      "-U", DEFAULT_PG_USER,
      "-f", input.toString(),
      "-o", actual.toString()
    ));

    compareExpectedAndActual(expected, actual);
  }

  void testPgDumpHelper(final String binaryName,
                        final String inputFileRelativePath,
                        final String outputFileRelativePath,
                        final String expectedFileRelativePath,
                        final boolean includeYbMetadata) throws Exception {
    // Location of Postgres regression tests
    File pgRegressDir = PgRegressBuilder.getPgRegressDir();

    // Create the data
    try (BufferedReader inputIn = createFileReader(new File(pgRegressDir,
                                                            inputFileRelativePath))) {
      try (Statement statement = connection.createStatement()) {
        String inputLine = null;
        while ((inputLine = inputIn.readLine()) != null) {
          LOG.info(inputLine);
          statement.execute(inputLine);
          LOG.info("Executed");
        }
      }
    }

    // Dump and validate the data
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, binaryName);

    final int tserverIndex = 0;
    File actual = new File(pgRegressDir, outputFileRelativePath);
    File expected = new File(pgRegressDir, expectedFileRelativePath);

    List<String> args = new ArrayList<>(Arrays.asList(
      ysqlDumpExec.toString(), "-h", getPgHost(tserverIndex),
      "-p", Integer.toString(getPgPort(tserverIndex)),
      "-U", DEFAULT_PG_USER,
      "-f", actual.toString(),
      "-m", getMasterLeaderAddress().toString()
    ));

    if (includeYbMetadata) {
      args.add("--include-yb-metadata");
    }

    buildAndRunProcess(binaryName, args);

    compareExpectedAndActual(expected, actual);
  }

  private int buildAndRunProcess(String logPrefix, List<String> args) throws Exception {
    ProcessBuilder pb = new ProcessBuilder(args);

    // Handle the logs output by ysqlsh.
    Process proc = pb.start();
    stdoutLogPrinter = new LogPrinter(proc.getInputStream(), logPrefix + "|stdout ");
    stderrLogPrinter = new LogPrinter(proc.getErrorStream(), logPrefix + "|stderr ");

    // Wait for the process to complete.
    int exitCode = proc.waitFor();
    stdoutLogPrinter.stop();
    stderrLogPrinter.stop();
    return exitCode;
  }

  private void compareExpectedAndActual(File expected, File actual) throws Exception {
    // Compare the expected output and the actual output.
    try (BufferedReader actualIn   = createFileReader(actual);
         BufferedReader expectedIn = createFileReader(expected);) {

      // Create the side-by-side diff between the actual output and expected output.
      // The resulting string will be used to provide debug information if the below
      // comparison between the two files fails.
      String message = "Side-by-side diff between expected output and actual output:\n" +
            new SideBySideDiff(actual, expected).getSideBySideDiff();

      // Compare the actual output and expected output.
      String actualLine = null, expectedLine = null;
      while ((actualLine = actualIn.readLine()) != null &&
             (expectedLine = expectedIn.readLine()) != null) {
        assertEquals(message,
                     postprocessOutputLine(expectedLine),
                     postprocessOutputLine(actualLine));
      }
      expectOnlyEmptyLines(message, actualLine, actualIn);
      expectOnlyEmptyLines(message, expectedLine, expectedIn);
    }
  }

  private BufferedReader createFileReader(File f) throws Exception {
    return new BufferedReader(new FileReader(f));
  }
}
