// Copyright (c) YugabyteDB, Inc.
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

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.sql.Statement;

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

/**
 * TestYsqlDump
 *    Tests by loading schema from files in src/postgres/src/test/regress/sql,
 *    taking a ysql dump / dumpall, then comparing the output to the expected
 *    files in src/postgres/src/test/regress/data.
 *
 *    Some of the tests then import the dump into a fresh db/cluster and run
 *    describe commands from src/postgres/src/test/regress/sql/ and compare
 *    the output to th expected files in src/postgres/src/test/regress/expected.
 *
 *    Ideally, all tests would import the dump back into a fresh cluster but it
 *    is not always possible. For ex, ysql_dumpall outputs CREATE ROLE postgres
 *    which always fails on a new cluster.
 *
 */
@RunWith(value=YBTestRunnerNonTsanAsan.class)
public class TestYsqlDump extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYsqlDump.class);

  private static enum IncludeYbMetadata { ON, OFF }
  private static enum NoTableSpaces { ON, OFF }
  private static enum DumpRoleChecks { ON, OFF }

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
    flagMap.put("ysql_enable_profile", "true");
    return flagMap;
  }

  /*
   * The following logic is needed to remove the dependency on the exact version number from
   * the ysql_dump output part that looks like this:
   * -- Dumped from database version 11.2-YB-1.3.2.0-b0
   * -- Dumped by ysql_dump version 11.2-YB-1.3.2.0-b0
   */

  private static Pattern VERSION_NUMBER_PATTERN = Pattern.compile(
      " version [0-9]+[.][0-9]+-YB-([0-9]+[.]){3}[0-9]+-b[0-9]+");

  private static String  VERSION_NUMBER_REPLACEMENT_STR =
      " version X.X-YB-X.X.X.X-bX";

  private static String postprocessOutputLine(String s) {
    if (s == null)
      return null;

    // First handle version number replacement
    String processed = VERSION_NUMBER_PATTERN.matcher(s).replaceAll(VERSION_NUMBER_REPLACEMENT_STR);

    /*
      Handle SCRAM password wildcards - replace specific SCRAM hashes with '*' to match expected
      files.
      SCRAM format: SCRAM-SHA-256$4096:salt$storedkey:serverkey
      We replace the salt, storedkey, and serverkey parts with '*' to match the wildcard in
      expected files
     */
    processed = processed.replaceAll(
        "SCRAM-SHA-256\\$4096:[a-zA-Z0-9+/=]+\\$[a-zA-Z0-9+/=]+:[a-zA-Z0-9+/=]+",
        "SCRAM-SHA-256\\$4096:*");

    return StringUtil.expandTabsAndRemoveTrailingSpaces(processed);
  }

  @Test
  public void ysqlDumpWithYbMetadata() throws Exception {
    restartCluster();

    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dump.sql" /* inputFileRelativePath */,
        "data/yb_ysql_dump.data.sql" /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dump.out" /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.OFF);

    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate(String.format("CREATE DATABASE import_db;"));
    }

    verifyYsqlDump(
      true /* importDump */,
      "import_db" /* verifyDbName */,
      "results/yb.orig.ysql_dump.out" /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_describe.sql" /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dump_describe.out" /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dump_describe.out" /* outputDescribeFileRelativePath */);
  }

  @Test
  public void ysqlDumpWithDumpRoleChecks() throws Exception {
    restartCluster();

    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dump.sql" /* inputFileRelativePath */,
        "data/yb_ysql_dump_with_dump_role_checks.data.sql" /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dump.out" /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.OFF,
        DumpRoleChecks.ON);

    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate(String.format("CREATE DATABASE import_db;"));
    }

    verifyYsqlDump(
      true /* importDump */,
      "import_db" /* verifyDbName */,
      "results/yb.orig.ysql_dump.out" /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_describe.sql" /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dump_describe.out" /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dump_describe.out" /* outputDescribeFileRelativePath */);
  }

  @Test
  public void ysqlDumpAllWithYbMetadata() throws Exception {
    // Configure MD5 password encryption to maintain compatibility with expected output
    restartClusterWithClusterBuilder(cb -> {
      cb.addCommonTServerFlag("ysql_pg_conf_csv", "password_encryption=md5");
    });

    // Force yugabyte user to use MD5 password format (since default is now SCRAM)
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET password_encryption = 'md5'");
      stmt.execute("ALTER ROLE yugabyte PASSWORD 'yugabyte'");
    }

    // Note that we're using the same describe input as for regular ysql_dump!
    ysqlDumpTester(
        "ysql_dumpall" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dumpall.sql" /* inputFileRelativePath */,
        "data/yb_ysql_dumpall.data.sql" /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dumpall.out" /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.OFF);

    // ysql_dumpall cannot be imported as it has DDL that cannot be repeated
    // like CREATE ROLE postgres
    verifyYsqlDump(
      false /* importDump*/,
      "" /* verifyDbName */,
      "results/yb.orig.ysql_dumpall.out" /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_describe.sql" /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dumpall_describe.out" /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dumpall_describe.out" /* outputDescribeFileRelativePath */
    );
  }

  @Test
  public void ysqlDumpAllWithDumpRoleChecks() throws Exception {
    // Configure MD5 password encryption to maintain compatibility with expected output
    restartClusterWithClusterBuilder(cb -> {
      cb.addCommonTServerFlag("ysql_pg_conf_csv", "password_encryption=md5");
    });

    // Force yugabyte user to use MD5 password format (since default is now SCRAM)
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET password_encryption = 'md5'");
      stmt.execute("ALTER ROLE yugabyte PASSWORD 'yugabyte'");
    }

    ysqlDumpTester(
        "ysql_dumpall" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dumpall.sql" /* inputFileRelativePath */,
        "data/yb_ysql_dumpall.data.sql" /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dumpall.out" /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.OFF);

    // ysql_dumpall cannot be imported as it has DDL that cannot be repeated
    // like CREATE ROLE postgres
    verifyYsqlDump(
      false /* importDump*/,
      "" /* verifyDbName */,
      "results/yb.orig.ysql_dumpall.out" /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_describe.sql" /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dumpall_describe.out" /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dumpall_describe.out" /* outputDescribeFileRelativePath */
    );
  }

  @Test
  public void ysqlDumpAllRoleProfilesWithMD5Passwords() throws Exception {
    restartClusterWithClusterBuilder(cb -> {
      cb.addCommonFlag("ysql_enable_profile", "true");
      cb.addCommonTServerFlag("ysql_pg_conf_csv", "password_encryption=md5");
      cb.addCommonTServerFlag("ysql_hba_conf_csv",
          "host all yugabyte_test 0.0.0.0/0 trust," +
          "host all yugabyte 0.0.0.0/0 trust," +
          "host all all 0.0.0.0/0 md5");
    });
    LOG.info("created mini cluster");

    // Force yugabyte user to use MD5 password format (since default is now SCRAM)
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET password_encryption = 'md5'");
      stmt.execute("ALTER ROLE yugabyte PASSWORD 'yugabyte'");
    }

    ysqlDumpTester(
        "ysql_dumpall" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dumpall_profile_and_role_profiles.sql"
        /* inputFileRelativePath */,
        "data/yb_ysql_dumpall_profile_and_role_profiles.data.sql"
        /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dumpall_profile_and_role_profiles.out"
        /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.ON);

    // ysql_dumpall cannot be imported as it has DDL that cannot be repeated
    // like CREATE ROLE postgres
    verifyYsqlDump(
      false /*importDump*/,
      "" /* verifyDbName */,
      "results/yb.orig.ysql_dumpall_profile_and_role_profiles.out"
      /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dumpall_describe_profile_and_role_profiles.sql"
      /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dumpall_describe_profile_and_role_profiles.out"
      /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dumpall_describe_profile_and_role_profiles.out"
      /* outputDescribeFileRelativePath */
    );
  }

  @Test
  public void ysqlDumpAllRoleProfilesWithSCRAMPasswords() throws Exception {
    restartClusterWithClusterBuilder(cb -> {
      cb.addCommonFlag("ysql_enable_profile", "true");
      cb.addCommonTServerFlag("ysql_hba_conf_csv",
          "host all yugabyte_test 0.0.0.0/0 trust," +
          "host all yugabyte 0.0.0.0/0 trust," +
          "host all all 0.0.0.0/0 scram-sha-256");
    });
    LOG.info("created mini cluster");

    // Force yugabyte user to use SCRAM password format with fixed hash
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("ALTER ROLE yugabyte PASSWORD '" +
          "SCRAM-SHA-256$4096:VLK4RMaQLCvNtQ==$6YtlR4t69SguDiwFvbVgVZtuz6gpJQQqUMZ7IQJK5yI=:" +
          "ps75jrHeYU4lXCcXI4O8oIdJ3eO8o2jirjruw9phBTo='");
    }

    ysqlDumpTester(
        "ysql_dumpall" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dumpall_profile_and_role_profiles_scram.sql"
        /* inputFileRelativePath */,
        "data/yb_ysql_dumpall_profile_and_role_profiles_scram.data.sql"
        /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dumpall_profile_and_role_profiles_scram.out"
        /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.ON);

    // ysql_dumpall cannot be imported as it has DDL that cannot be repeated
    // like CREATE ROLE postgres
    verifyYsqlDump(
      false /*importDump*/,
      "" /* verifyDbName */,
      "results/yb.orig.ysql_dumpall_profile_and_role_profiles_scram.out"
      /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dumpall_describe_profile_and_role_profiles.sql"
      /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dumpall_describe_profile_and_role_profiles.out"
      /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dumpall_describe_profile_and_role_profiles_scram.out"
      /* outputDescribeFileRelativePath */
    );
  }

  @Test
  public void ysqlDumpWithoutYbMetadata() throws Exception {

    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dump_without_ybmetadata.sql" /* inputFileRelativePath */,
        "data/yb_ysql_dump_without_ybmetadata.data.sql" /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dump_without_ybmetadata.out" /* outputFileRelativePath */,
        IncludeYbMetadata.OFF,
        NoTableSpaces.OFF);

    restartCluster();

    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate(String.format("CREATE DATABASE import_db;"));
    }

    verifyYsqlDump(
      true /* importDump */,
      "import_db" /* verifyDbName */,
      "results/yb.orig.ysql_dump_without_ybmetadata.out" /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_without_ybmetadata_describe.sql" /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dump_without_ybmetadata_describe.out"
      /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dump_without_ybmetadata_describe.out"
      /* outputDescribeFileRelativePath */);
  }

  @Test
  public void ysqlDumpAllWithoutYbMetadata() throws Exception {
    // Configure MD5 password encryption to maintain compatibility with expected output
    restartClusterWithClusterBuilder(cb -> {
      cb.addCommonTServerFlag("ysql_pg_conf_csv", "password_encryption=md5");
    });

    // Force yugabyte user to use MD5 password format (since default is now SCRAM)
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET password_encryption = 'md5'");
      stmt.execute("ALTER ROLE yugabyte PASSWORD 'yugabyte'");
    }

    ysqlDumpTester(
        "ysql_dumpall" /* binaryName */,
        "" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dumpall.sql" /* inputFileRelativePath */,
        "data/yb_ysql_dumpall_without_ybmetadata.data.sql" /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dumpall_without_ybmetadata.out" /* outputFileRelativePath */,
        IncludeYbMetadata.OFF,
        NoTableSpaces.OFF);

    // ysql_dumpall cannot be imported as it has DDL that cannot be repeated
    // like CREATE ROLE postgres
    verifyYsqlDump(
      false /*importDump*/,
      "" /* verifyDbName */,
      "results/yb.orig.ysql_dumpall_without_ybmetadata.out" /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_describe.sql" /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dumpall_describe.out" /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dumpall_without_ybmetadata_describe.out"
      /* outputDescribeFileRelativePath */);
  }

  @Test
  public void ysqlDumpColocatedDB() throws Exception {
    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "colocated_db" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dump_colocated_database.sql" /* inputFileRelativePath */,
        "data/yb_ysql_dump_colocated_database.data.sql" /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dump_colocated_database.out" /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.OFF);

    restartCluster();

    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate(String.format(
        "CREATE DATABASE %s WITH colocation = true", "colocated_db"));
    }

    verifyYsqlDump(
      true /*importDump*/,
      "colocated_db" /*verifyDbName*/,
      "results/yb.orig.ysql_dump_colocated_database.out" /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_describe_colocated_database.sql" /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dump_describe_colocated_database.out"
      /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dump_describe_colocated_database.out"
      /* outputDescribeFileRelativePath */);
  }

  @Test
  public void ysqlDumpColocatedTablesWithTablespaces() throws Exception {
    restartClusterWithClusterBuilder(cb -> {
      cb.addCommonFlag("ysql_enable_colocated_tables_with_tablespaces", "true");
      cb.addCommonTServerFlag("placement_cloud", "testCloud");
      cb.addCommonTServerFlag("placement_region", "testRegion");
      cb.perTServerFlags(Arrays.asList(
          Collections.singletonMap("placement_zone", "testZone1"),
          Collections.singletonMap("placement_zone", "testZone2"),
          Collections.singletonMap("placement_zone", "testZone3")));
    });
    LOG.info("created mini cluster");

    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "colo_tables" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dump_colocated_tables_with_tablespaces.sql"
        /* inputFileRelativePath */,
        "data/yb_ysql_dump_colocated_tables_with_tablespaces.data.sql"
        /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dump_colocated_tables_with_tablespaces.out"
        /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.ON);


    // The resulting dump cannot be imported due to #25299
    verifyYsqlDump(
      false /*importDump*/,
      "colo_tables" /* dumpedDatabaseName */,
      "results/yb.orig.ysql_dump_colocated_tables_with_tablespaces.out"
      /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_describe_colocated_tables_with_tablespaces.sql"
      /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dump_describe_colocated_tables_with_tablespaces.out"
      /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dump_describe_colocated_tables_with_tablespaces.out"
      /* outputDescribeFileRelativePath */
    );
  }

  @Test
  public void ysqlDumpRespectsPGHOSTEnvVar() throws Exception {
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, "ysql_dump");

    String invalidHost = "nosuchhost.invalid";

    List<String> args = Arrays.asList(
        ysqlDumpExec.toString(),
        "-d", "testdb"
    );

    ProcessBuilder pb = new ProcessBuilder(args);
    pb.environment().put("PGHOST", invalidHost);
    pb.redirectErrorStream(true);

    LOG.info("Running ysql_dump with PGHOST=" + invalidHost);
    Process process = pb.start();

    // Capture output (stdout + stderr merged)
    BufferedReader reader = new BufferedReader(
        new InputStreamReader(process.getInputStream()));
    StringBuilder output = new StringBuilder();
    String line;
    while ((line = reader.readLine()) != null) {
      output.append(line).append("\n");
    }

    process.waitFor(60, TimeUnit.SECONDS);

    // The process should fail (non-zero exit code)
    assertNotEquals("ysql_dump should fail when PGHOST points to invalid host",
        0, process.exitValue());

    // The error message should contain the invalid host name, proving PGHOST was respected
    String outputStr = output.toString();
    assertTrue("Error message should mention the invalid PGHOST value '" + invalidHost +
        "', but got: " + outputStr,
        outputStr.contains(invalidHost));

    LOG.info("ysql_dump correctly respected PGHOST environment variable");
  }

  /**
   * Test that the --host flag takes precedence over the PGHOST environment variable.
   * When both are set, ysql_dump should use the --host value and succeed.
   */
  @Test
  public void ysqlDumpHostFlagOverridesPGHOSTEnvVar() throws Exception {
    int tserverIndex = 0;
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, "ysql_dump");

    // Set PGHOST to an invalid host - if this were used, the dump would fail
    String invalidEnvHost = "invalid-env-host.invalid";

    List<String> args = Arrays.asList(
        ysqlDumpExec.toString(),
        "-h", getPgHost(tserverIndex),
        "-p", Integer.toString(getPgPort(tserverIndex)),
        "-U", DEFAULT_PG_USER,
        "-d", DEFAULT_PG_DATABASE
    );

    ProcessBuilder pb = new ProcessBuilder(args);
    pb.environment().put("PGHOST", invalidEnvHost);
    pb.redirectErrorStream(true);

    LOG.info("Running ysql_dump with PGHOST=" + invalidEnvHost +
        " and -h " + getPgHost(tserverIndex));
    Process process = pb.start();

    // Capture output (stdout + stderr merged)
    BufferedReader reader = new BufferedReader(
        new InputStreamReader(process.getInputStream()));
    StringBuilder output = new StringBuilder();
    String line;
    while ((line = reader.readLine()) != null) {
      output.append(line).append("\n");
    }

    process.waitFor(60, TimeUnit.SECONDS);

    // The process should succeed (exit code 0) because -h flag overrides PGHOST
    assertEquals("ysql_dump should succeed when -h flag points to valid host, " +
        "even with invalid PGHOST. Output: " + output.toString(),
        0, process.exitValue());

    LOG.info("ysql_dump correctly prioritized --host flag over PGHOST environment variable");
  }

  @Test
  public void ysqlDumpLegacyColocatedDB() throws Exception {
    restartClusterWithFlags(Collections.singletonMap("ysql_legacy_colocated_database_creation",
                                                     "true"),
                            Collections.emptyMap());
    // Reuse the same inputFileRelativePath and inputDescribeFileRelativePath
    // as test ysqlDumpColocatedDB.
    ysqlDumpTester(
        "ysql_dump" /* binaryName */,
        "colocated_db" /* dumpedDatabaseName */,
        "sql/yb.orig.ysql_dump_colocated_database.sql" /* inputFileRelativePath */,
        "data/yb_ysql_dump_legacy_colocated_database.data.sql" /* expectedDumpRelativePath */,
        "results/yb.orig.ysql_dump_legacy_colocated_database.out" /* outputFileRelativePath */,
        IncludeYbMetadata.ON,
        NoTableSpaces.OFF);

    restartClusterWithFlags(
      Collections.singletonMap(
        "ysql_legacy_colocated_database_creation", "true"),
      Collections.emptyMap());

    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate(String.format(
        "CREATE DATABASE %s WITH colocation = true", "colocated_db"));
    }

    verifyYsqlDump(
      true/* importDump */,
      "colocated_db" /* dumpedDatabaseName */,
      "results/yb.orig.ysql_dump_legacy_colocated_database.out"
      /* outputFileRelativePath */,
      "sql/yb.orig.ysql_dump_describe_colocated_database.sql"
      /* inputDescribeFileRelativePath */,
      "expected/yb.orig.ysql_dump_describe_legacy_colocated_database.out"
      /* expectedDescribeFileRelativePath */,
      "results/yb.orig.ysql_dump_describe_legacy_colocated_database.out"
      /* outputDescribeFileRelativePath */
    );
  }

  void ysqlDumpTester(final String binaryName,
                      final String dumpedDatabaseName,
                      final String inputFileRelativePath,
                      final String expectedDumpRelativePath,
                      final String outputFileRelativePath,
                      final IncludeYbMetadata includeYbMetadata,
                      final NoTableSpaces noTableSpaces) throws Exception {
    ysqlDumpTester(
        binaryName, dumpedDatabaseName, inputFileRelativePath, expectedDumpRelativePath,
        outputFileRelativePath, includeYbMetadata, noTableSpaces, DumpRoleChecks.OFF);
  }

  void ysqlDumpTester(final String binaryName,
                      final String dumpedDatabaseName,
                      final String inputFileRelativePath,
                      final String expectedDumpRelativePath,
                      final String outputFileRelativePath,
                      final IncludeYbMetadata includeYbMetadata,
                      final NoTableSpaces noTableSpaces,
                      final DumpRoleChecks dumpRoleChecks) throws Exception {
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

    // Get a ysql dump
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
    if (noTableSpaces == NoTableSpaces.ON) {
      args.add("--no-tablespaces");
    }
    if (dumpRoleChecks == DumpRoleChecks.ON) {
      args.add("--dump-role-checks");
    }

    if (!dumpedDatabaseName.isEmpty()) {
      Collections.addAll(args, "-d", dumpedDatabaseName);
    }
    ProcessUtil.executeSimple(args, binaryName);

    // Verify the dump matches what is expected
    assertOutputFile(expected, actual);

  }

  /** Compare the expected output and the actual output. */
  public static void assertOutputFile(File expected, File actual) throws IOException {
    List<String> expectedLines = FileUtils.readLines(expected, StandardCharsets.UTF_8);
    List<String> actualLines   = FileUtils.readLines(actual, StandardCharsets.UTF_8);

    // Create the side-by-side diff between the actual output and expected output.
    // The resulting string will be used to provide debug information if the below
    // comparison between the two files fails.
    String message = "Side-by-side diff between expected output from " +
      expected.getAbsolutePath() + " and actual output from: " + actual.getAbsolutePath() + " \n" +
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

  private static void assertOnlyEmptyLines(String message, List<String> lines) {
    Set<String> processedLinesSet =
        lines.stream().map((l) -> l.trim()).collect(Collectors.toSet());
    assertTrue(message, Sets.newHashSet("").containsAll(processedLinesSet));
  }

  void verifyYsqlDump(
    boolean importDump,
    final String verifyDbName,
    final String outputFileRelativePath,
    final String inputDescribeFileRelativePath,
    final String expectedDescribeFileRelativePath,
    final String outputDescribeFileRelativePath) throws Exception {

    // Location of Postgres regression tests
    File pgRegressDir = PgRegressBuilder.PG_REGRESS_DIR;
    int tserverIndex = 0;
    File ysqlshExec = new File(pgBinDir, "ysqlsh");
    File actual   = new File(pgRegressDir, outputFileRelativePath);
    File inputDesc    = new File(pgRegressDir, inputDescribeFileRelativePath);
    File expectedDesc = new File(pgRegressDir, expectedDescribeFileRelativePath);
    File actualDesc   = new File(pgRegressDir, outputDescribeFileRelativePath);
    actualDesc.getParentFile().mkdirs();

    if (importDump) {
      // Import the ysql dump, raising errors
      List<String> ysqlsh_import_args = new ArrayList<>(Arrays.asList(
        ysqlshExec.toString(),
        "-h", getPgHost(tserverIndex),
        "-p", Integer.toString(getPgPort(tserverIndex)),
        "-U", DEFAULT_PG_USER,
        "-f", actual.getAbsolutePath(),
        "--echo-all",
        "-v", "ON_ERROR_STOP=1"
      ));

      if (!verifyDbName.isEmpty()) {
        ysqlsh_import_args.add("-d");
        ysqlsh_import_args.add(verifyDbName);
      }

      LOG.info("Importing ysql dump " + ysqlsh_import_args.toString());
      ProcessUtil.executeSimple(ysqlsh_import_args, "ysqlsh (import)");
    }

    // Run some validations
    List<String> ysqlsh_args = new ArrayList<>(Arrays.asList(
      ysqlshExec.toString(),
      "-h", getPgHost(tserverIndex),
      "-p", Integer.toString(getPgPort(tserverIndex)),
      "-U", DEFAULT_PG_USER,
      "-f", inputDesc.toString(),
      "-o", actualDesc.toString()
    ));
    if (!verifyDbName.isEmpty()) {
      ysqlsh_args.add("-d");
      ysqlsh_args.add(verifyDbName);
    }
    ProcessUtil.executeSimple(ysqlsh_args, "ysqlsh (validate describes)");

    assertOutputFile(expectedDesc, actualDesc);
  }

}
