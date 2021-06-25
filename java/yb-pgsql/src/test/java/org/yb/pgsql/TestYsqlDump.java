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
                     "ysql_dump_stdout.txt" /* stdoutFileRelativePath */);
  }

  @Test
  public void testPgDumpAll() throws Exception {
    testPgDumpHelper("ysql_dumpall" /* binaryName */,
                     "sql/yb_ysql_dumpall.sql" /* inputFileRelativePath */,
                     "output/yb_ysql_dumpall.out" /* outputFileRelativePath */,
                     "expected/yb_ysql_dumpall.out" /* expectedFileRelativePath */,
                     "ysql_dumpall_stdout.txt" /* stdoutFileRelativePath */);
  }

  void testPgDumpHelper(final String binaryName,
                        final String inputFileRelativePath,
                        final String outputFileRelativePath,
                        final String expectedFileRelativePath,
                        final String stdoutFileRelativePath) throws Exception {
    // Location of Postgres regression tests
    File pgRegressDir = PgRegressRunner.getPgRegressDir();

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
    File pgBinDir = PgRegressRunner.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, binaryName);

    final int tserverIndex = 0;
    File actual = new File(pgRegressDir, outputFileRelativePath);
    File expected = new File(pgRegressDir, expectedFileRelativePath);
    ProcessBuilder pb = new ProcessBuilder(ysqlDumpExec.toString(), "-h", getPgHost(tserverIndex),
                                           "-p", Integer.toString(getPgPort(tserverIndex)),
                                           "-U", DEFAULT_PG_USER,
                                           "-f", actual.toString(),
                                           "-m", getMasterLeaderAddress().toString());

    // Handle the logs output by ysql_dump.
    String logPrefix = "ysql_dump";
    Process ysqlDumpProc = pb.start();
    stdoutLogPrinter = new LogPrinter(
        ysqlDumpProc.getInputStream(),
        logPrefix + "|stdout ");
    stderrLogPrinter = new LogPrinter(
        ysqlDumpProc.getErrorStream(),
        logPrefix + "|stderr ");

    // Wait for the process to complete.
    int exitCode = ysqlDumpProc.waitFor();
    stdoutLogPrinter.stop();
    stderrLogPrinter.stop();

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
