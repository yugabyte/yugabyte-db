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

import static org.yb.AssertionWrappers.*;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

import org.apache.commons.io.FileUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.ProcessUtil;
import org.yb.util.SideBySideDiff;
import org.yb.util.StringUtil;
import org.yb.util.YBTestRunnerNonTsanAsan;

import com.google.common.collect.Sets;

@RunWith(value=YBTestRunnerNonTsanAsan.class)
public class TestYsqlDump extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYsqlDump.class);

  private static enum IncludeYbMetadata { ON, OFF }

  @Override
  public int getTestMethodTimeoutSec() {
    return super.getTestMethodTimeoutSec() * 10;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("TEST_sequential_colocation_ids", "true");
    flagMap.put("ysql_legacy_colocated_database_creation", "false");
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // Turn off sequence cache.
    flagMap.put("ysql_sequence_cache_minval", "0");
    return flagMap;
  }

  // The following logic is needed to remove the dependency on the exact version number from
  // the ysql_dump output part that looks like this:
  // -- Dumped from database version 11.2-YB-1.3.2.0-b0
  // -- Dumped by ysql_dump version 11.2-YB-1.3.2.0-b0

  private static Pattern VERSION_NUMBER_PATTERN = Pattern.compile(
      " version [0-9]+[.][0-9]+-YB-([0-9]+[.]){3}[0-9]+-b[0-9]+");

  private static String  VERSION_NUMBER_REPLACEMENT_STR =
      " version X.X-YB-X.X.X.X-bX";

  private String postprocessOutputLine(String s) {
    if (s == null)
      return null;
    return StringUtil.expandTabsAndRemoveTrailingSpaces(
      VERSION_NUMBER_PATTERN.matcher(s).replaceAll(VERSION_NUMBER_REPLACEMENT_STR));
  }

  @Test
  public void ysqlDumpWithYbMetadata() throws Exception {
    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb_ysql_dump.sql" /* inputFileRelativePath */,
        "sql/yb_ysql_dump_describe.sql" /* inputDescribeFileRelativePath */,
        "data/yb_ysql_dump.data.sql" /* expectedDumpRelativePath */,
        "expected/yb_ysql_dump_describe.out" /* expectedDescribeFileRelativePath */,
        "results/yb_ysql_dump.out" /* outputFileRelativePath */,
        "results/yb_ysql_dump_describe.out" /* outputDescribeFileRelativePath */,
        IncludeYbMetadata.ON);
  }

  @Test
  public void ysqlDumpAllWithYbMetadata() throws Exception {
    // Note that we're using the same describe input as for regular ysql_dump!
    ysqlDumpTester(
        "ysql_dumpall" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb_ysql_dumpall.sql" /* inputFileRelativePath */,
        "sql/yb_ysql_dump_describe.sql" /* inputDescribeFileRelativePath */,
        "data/yb_ysql_dumpall.data.sql" /* expectedDumpRelativePath */,
        "expected/yb_ysql_dumpall_describe.out" /* expectedDescribeFileRelativePath */,
        "results/yb_ysql_dumpall.out" /* outputFileRelativePath */,
        "results/yb_ysql_dumpall_describe.out" /* outputDescribeFileRelativePath */,
        IncludeYbMetadata.ON);
  }

  @Test
  public void ysqlDumpWithoutYbMetadata() throws Exception {
    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb_ysql_dump_without_ybmetadata.sql" /* inputFileRelativePath */,
        "sql/yb_ysql_dump_without_ybmetadata_describe.sql" /* inputDescribeFileRelativePath */,
        "data/yb_ysql_dump_without_ybmetadata.data.sql" /* expectedDumpRelativePath */,
        "expected/yb_ysql_dump_without_ybmetadata_describe.out"
        /* expectedDescribeFileRelativePath */,
        "results/yb_ysql_dump_without_ybmetadata.out" /* outputFileRelativePath */,
        "results/yb_ysql_dump_without_ybmetadata_describe.out"
        /* outputDescribeFileRelativePath */,
        IncludeYbMetadata.OFF);
  }

  @Test
  public void ysqlDumpColocatedDB() throws Exception {
    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "colocated_db" /* dumpedDatabaseName */,
        "sql/yb_ysql_dump_colocated_database.sql" /* inputFileRelativePath */,
        "sql/yb_ysql_dump_describe_colocated_database.sql" /* inputDescribeFileRelativePath */,
        "data/yb_ysql_dump_colocated_database.data.sql" /* expectedDumpRelativePath */,
        "expected/yb_ysql_dump_describe_colocated_database.out"
        /* expectedDescribeFileRelativePath */,
        "results/yb_ysql_dump_colocated_database.out" /* outputFileRelativePath */,
        "results/yb_ysql_dump_describe_colocated_database.out" /* outputDescribeFileRelativePath */,
        IncludeYbMetadata.ON);
  }

  @Test
  public void ysqlDumpLegacyColocatedDB() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    // Reuse the same inputFileRelativePath and inputDescribeFileRelativePath
    // as test ysqlDumpColocatedDB.
    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "colocated_db" /* dumpedDatabaseName */,
        "sql/yb_ysql_dump_colocated_database.sql" /* inputFileRelativePath */,
        "sql/yb_ysql_dump_describe_colocated_database.sql" /* inputDescribeFileRelativePath */,
        "data/yb_ysql_dump_legacy_colocated_database.data.sql" /* expectedDumpRelativePath */,
        "expected/yb_ysql_dump_describe_legacy_colocated_database.out"
        /* expectedDescribeFileRelativePath */,
        "results/yb_ysql_dump_legacy_colocated_database.out" /* outputFileRelativePath */,
        "results/yb_ysql_dump_describe_legacy_colocated_database.out"
        /* outputDescribeFileRelativePath */,
        IncludeYbMetadata.ON);
  }

  void ysqlDumpTester(final String binaryName,
                      final String dumpedDatabaseName,
                      final String inputFileRelativePath,
                      final String inputDescribeFileRelativePath,
                      final String expectedDumpRelativePath,
                      final String expectedDescribeFileRelativePath,
                      final String outputFileRelativePath,
                      final String outputDescribeFileRelativePath,
                      final IncludeYbMetadata includeYbMetadata) throws Exception {
    // Location of Postgres regression tests
    File pgRegressDir = PgRegressBuilder.PG_REGRESS_DIR;

    // Create the data
    int tserverIndex = 0;
    File ysqlshExec = new File(pgBinDir, "ysqlsh");
    File inputFile  = new File(pgRegressDir, inputFileRelativePath);
    ProcessUtil.executeSimple(Arrays.asList(
      ysqlshExec.toString(),
      "-h", getPgHost(tserverIndex),
      "-p", Integer.toString(getPgPort(tserverIndex)),
      "-U", TEST_PG_USER,
      "-f", inputFile.toString()
    ), "ysqlsh");

    // Dump and validate the data
    File pgBinDir     = PgRegressBuilder.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, binaryName);

    File expected = new File(pgRegressDir, expectedDumpRelativePath);
    File actual   = new File(pgRegressDir, outputFileRelativePath);
    actual.getParentFile().mkdirs();

    List<String> args = new ArrayList<>(Arrays.asList(
      ysqlDumpExec.toString(),
      "-h", getPgHost(tserverIndex),
      "-p", Integer.toString(getPgPort(tserverIndex)),
      "-U", DEFAULT_PG_USER,
      "-f", actual.toString()
    ));
    if (includeYbMetadata == IncludeYbMetadata.ON) {
      args.add("--include-yb-metadata");
    }
    if (!dumpedDatabaseName.isEmpty()) {
      Collections.addAll(args, "-d", dumpedDatabaseName);
    }
    ProcessUtil.executeSimple(args, binaryName);

    assertOutputFile(expected, actual);

    File inputDesc    = new File(pgRegressDir, inputDescribeFileRelativePath);
    File expectedDesc = new File(pgRegressDir, expectedDescribeFileRelativePath);
    File actualDesc   = new File(pgRegressDir, outputDescribeFileRelativePath);
    actualDesc.getParentFile().mkdirs();

    // Run some validations
    List<String> ysqlsh_args = new ArrayList<>(Arrays.asList(
      ysqlshExec.toString(),
      "-h", getPgHost(tserverIndex),
      "-p", Integer.toString(getPgPort(tserverIndex)),
      "-U", DEFAULT_PG_USER,
      "-f", inputDesc.toString(),
      "-o", actualDesc.toString()
    ));
    if (!dumpedDatabaseName.isEmpty()) {
      Collections.addAll(ysqlsh_args, "-d", dumpedDatabaseName);
    }
    ProcessUtil.executeSimple(ysqlsh_args, "ysqlsh (validate describes)");

    assertOutputFile(expectedDesc, actualDesc);
  }

  /** Compare the expected output and the actual output. */
  private void assertOutputFile(File expected, File actual) throws IOException {
    List<String> expectedLines = FileUtils.readLines(expected, StandardCharsets.UTF_8);
    List<String> actualLines   = FileUtils.readLines(actual, StandardCharsets.UTF_8);

    // Create the side-by-side diff between the actual output and expected output.
    // The resulting string will be used to provide debug information if the below
    // comparison between the two files fails.
    String message = "Side-by-side diff between expected output and actual output:\n" +
          SideBySideDiff.generate(expected, actual) + "\n";

    int i = 0;
    for (; i < expectedLines.size() && i < actualLines.size(); ++i) {
      assertEquals(message,
                   postprocessOutputLine(expectedLines.get(i)),
                   postprocessOutputLine(actualLines.get(i)));
    }
    assertOnlyEmptyLines(message, expectedLines.subList(i, expectedLines.size()));
    assertOnlyEmptyLines(message, actualLines.subList(i, actualLines.size()));
  }

  private void assertOnlyEmptyLines(String message, List<String> lines) {
    Set<String> processedLinesSet =
        lines.stream().map((l) -> l.trim()).collect(Collectors.toSet());
    assertTrue(message, Sets.newHashSet("").containsAll(processedLinesSet));
  }
}
