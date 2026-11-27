// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.CONSISTENCY_CHECK_TABLE_NAME;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.NodeUniverseManager.RemoteBashRunSpec;
import com.yugabyte.yw.common.RemoteTransportSimulator.Transport;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

/**
 * Asserts that commands and script params built by NodeUniverseManager reach the node unchanged on
 * node agent, Kubernetes and SSH. Each transport quotes differently, so a scheme that suits one can
 * silently corrupt SQL on another; {@link RemoteTransportSimulator} models what each delivers.
 */
@RunWith(MockitoJUnitRunner.class)
public class NodeUniverseManagerTransportTest extends FakeDBApplication {

  /** A literal that must survive intact; this is the shape UpdateConsistencyCheck inserts. */
  private static final String QUERY_WITH_QUOTED_UUID =
      "INSERT INTO yba_consistency_check (seq_num, task_uuid) VALUES (0,"
          + " '24401e0e-eac8-434b-95ad-c23b254d4612')";

  private NodeUniverseManager nodeUniverseManager;
  private Universe universe;
  private final NodeDetails node = new NodeDetails();

  @Before
  public void setUp() {
    nodeUniverseManager = spy(new NodeUniverseManager());
    nodeUniverseManager.confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    doReturn("/home/yugabyte").when(nodeUniverseManager).getYbHomeDir(any(), any());

    Customer customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    node.nodeName = "u-n1";
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.1";
    node.ysqlServerRpcPort = 5433;
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              u.setUniverseDetails(universeDetails);
            });
  }

  private RemoteBashRunSpec ysqlSpec(String dbName, String query, boolean authEnabled) {
    return nodeUniverseManager.buildYsqlBatchRunSpec(
        node, universe, dbName, List.of(query), 300L, authEnabled, false, true);
  }

  /** Asserts the argv arrives unchanged on every transport that carries this command. */
  private void assertSurvivesAllTransports(List<String> command, boolean useBash) {
    for (Transport transport : Transport.values()) {
      assertEquals(
          transport + " corrupted the command",
          command,
          RemoteTransportSimulator.deliverViaRunCommand(transport, command, useBash));
    }
  }

  /** Asserts the script parameters arrive unchanged on every transport. */
  private void assertParamsSurviveAllTransports(List<String> params) {
    for (Transport transport : Transport.values()) {
      assertEquals(
          transport + " corrupted the script parameters",
          params,
          RemoteTransportSimulator.deliverViaRunScript(transport, params));
    }
  }

  @Test
  public void testYsqlBatchCommandSurvivesAllTransports() {
    assertSurvivesAllTransports(ysqlSpec("yugabyte", QUERY_WITH_QUOTED_UUID, false).command, false);
  }

  @Test
  public void testLegacyUseBashCommandSurvivesAllTransports() {
    // Guards the pre-existing callers that still rely on useBash=true.
    List<String> command = Arrays.asList("ls", "-la", "/home/yugabyte/master/logs");

    assertSurvivesAllTransports(command, true);
  }

  @Test
  public void testYbAdminCommandSurvivesAllTransports() {
    List<String> command =
        Arrays.asList(
            "/home/yugabyte/master/bin/yb-admin",
            "--master_addresses",
            "10.0.0.1:7100,10.0.0.2:7100",
            "-timeout_ms",
            "300000",
            "list_all_masters");

    assertSurvivesAllTransports(command, true);
  }

  @Test
  public void testConsistencyCheckUpdateQuerySurvivesAllTransports() {
    // UpdateConsistencyCheck runs this on every universe modification, and it is the query that
    // first surfaced the SSH corruption because it carries three quoted literals.
    String updateQuery =
        String.format(
            "WITH updated_rows AS (UPDATE yba_consistency_check SET seq_num = %d, task_uuid = '%s',"
                + " yw_uuid = '%s', yw_host = '%s' WHERE seq_num < %d RETURNING seq_num, task_uuid)"
                + " SELECT jsonb_agg(updated_rows) AS result FROM updated_rows;",
            7,
            "24401e0e-eac8-434b-95ad-c23b254d4612",
            "9f1c2c4e-1c2a-4a3b-8d3e-7a1b2c3d4e5f",
            "yba.example.com",
            7);

    assertSurvivesAllTransports(ysqlSpec("system_platform", updateQuery, false).command, false);
  }

  @Test
  public void testAdminRunQueryApiWithStringLiteralSurvivesAllTransports() {
    // The run_query admin API forwards arbitrary operator SQL through the same batch path.
    RemoteBashRunSpec spec =
        ysqlSpec(
            "yugabyte",
            "SELECT * FROM pg_roles WHERE rolname = 'yb_superuser' AND rolcanlogin;",
            true);

    assertSurvivesAllTransports(spec.command, false);
  }

  @Test
  public void testCheckCpuCgroupCommandSurvivesAllTransports() {
    // CheckCpuCgroup runs a bare script path with useBash=false, so it must be left untouched.
    List<String> command = Arrays.asList("/home/yugabyte/bin/check_cpu_cgroup.sh");

    assertSurvivesAllTransports(command, false);
    assertEquals(command, NodeUniverseManager.escapeForSshTransport(command, false));
  }

  @Test
  public void testNodeFileCollectorListingParamsSurviveAllTransports() {
    // NodeFileCollector and the v1 support bundle components both list files this way.
    List<String> params =
        Arrays.asList(
            "get_paths_and_sizes", "/mnt/d0/yb-data/tserver/logs", "1", "f", "/tmp/uuid-files.txt");

    assertParamsSurviveAllTransports(params);
  }

  @Test
  public void testSshTransportCorruptsQuotedLiteralsWithoutCompensation() {
    // The reason escapeForSshTransport has to exist: ssh.py wraps any space-containing argument in
    // single quotes without escaping the ones already inside it, so the UUID loses its quoting and
    // ysqlsh sees an unquoted identifier.
    RemoteBashRunSpec spec = ysqlSpec("yugabyte", QUERY_WITH_QUOTED_UUID, false);

    List<String> delivered =
        RemoteTransportSimulator.deliver(Transport.SSH, spec.command, false /* useBash */);

    assertNotEquals(spec.command, delivered);
    assertEquals(3, delivered.size());
    assertEquals("24401e0e-eac8-434b-95ad-c23b254d4612)\"", extractUuidToken(delivered.get(2)));
  }

  private static String extractUuidToken(String script) {
    int start = script.indexOf("24401e0e");
    return start < 0 ? "" : script.substring(start, Math.min(script.length(), start + 38));
  }

  @Test
  public void testRunScriptParamsSurviveAllTransports() {
    List<String> params =
        Arrays.asList(
            "collect_files",
            "--yb_home_dir",
            "/home/yugabyte",
            "--start_date",
            "2026-08-01T00:00:00Z",
            "--regex",
            "postgres .* log",
            "--label",
            "it's fine",
            "--empty",
            "");

    assertParamsSurviveAllTransports(params);
  }

  @Test
  public void testRunNodeActionScriptParamsNeedJavaSideQuoting() {
    // The reason quoteScriptParams has to exist: run_node_action.py joins the params with plain
    // spaces, so an unquoted value with a space becomes two positional parameters.
    List<String> params = Arrays.asList("collect_files", "--regex", "postgres .* log");

    assertEquals(
        Arrays.asList("collect_files", "--regex", "postgres", ".*", "log"),
        RemoteTransportSimulator.deliverPythonScriptParams(params));
  }

  @Test
  public void testEscapeForSshTransportEscapesSpacedArgsAndQuotesUnsafeOnes() {
    List<String> command =
        Arrays.asList(
            "bash",
            "-c",
            "ysqlsh -c \"SELECT * FROM t WHERE k = 'v'\"",
            "--flag=a'b",
            "/tmp/x-1.sh");

    assertEquals(
        Arrays.asList(
            "bash",
            "-c",
            "ysqlsh -c \"SELECT * FROM t WHERE k = '\"'\"'v'\"'\"'\"",
            // No space, so ssh.py leaves it bare: quoted here, or its quote would open a string.
            "'--flag=a'\"'\"'b'",
            "/tmp/x-1.sh"),
        NodeUniverseManager.escapeForSshTransport(command, false /* useBash */));
  }

  /**
   * The simulator splits words but runs nothing, so this goes through a real bash: the line ssh.py
   * hands the remote shell must deliver the same argv and run nothing. ValidateGFlags sends gflag
   * values this way.
   */
  @Test
  public void testShellCharactersReachTheProgramIntactOverSsh() throws Exception {
    List<String> command =
        Arrays.asList(
            "/home/yugabyte/tserver/bin/yb-tserver",
            "--version",
            "--ysql_pg_conf_csv=a;touch${IFS}injected",
            "$(touch${IFS}injected)",
            "`touch${IFS}injected`",
            "a|b&c>d<e",
            "*",
            "~",
            "{a,b}",
            "it's",
            "\"double\"",
            "back\\slash",
            "",
            "tab\there",
            "new\nline");
    Path dir = Files.createTempDirectory("ssh-escape");
    try {
      // A file for an unquoted * to expand to.
      Files.createFile(dir.resolve("marker"));
      String line =
          RemoteTransportSimulator.naiveJoin(
              NodeUniverseManager.escapeForSshTransport(command, false /* useBash */));
      Process process =
          new ProcessBuilder("bash", "-c", "printf '%s\\0' " + line)
              .directory(dir.toFile())
              .redirectErrorStream(true)
              .start();
      String out = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);

      assertEquals(out, 0, process.waitFor());
      List<String> received = new ArrayList<>(Arrays.asList(out.split("\0", -1)));
      received.remove(received.size() - 1);
      assertEquals(command, received);
      assertEquals(List.of("marker"), Arrays.asList(dir.toFile().list()));
    } finally {
      FileUtils.deleteDirectory(dir.toFile());
    }
  }

  @Test
  public void testEscapeForSshTransportLeavesUseBashCommandsAlone() {
    List<String> command = Arrays.asList("bash", "-c", "echo 'hello world'");

    assertEquals(command, NodeUniverseManager.escapeForSshTransport(command, true /* useBash */));
  }

  @Test
  public void testFallsBackToSshOnlyForShellCarryingCloudTypes() {
    assertFalse(NodeUniverseManager.fallsBackToSsh(CloudType.kubernetes));
    assertFalse(NodeUniverseManager.fallsBackToSsh(CloudType.local));
    assertFalse(NodeUniverseManager.fallsBackToSsh(CloudType.unknown));
    assertTrue(NodeUniverseManager.fallsBackToSsh(CloudType.aws));
    assertTrue(NodeUniverseManager.fallsBackToSsh(CloudType.gcp));
    assertTrue(NodeUniverseManager.fallsBackToSsh(CloudType.azu));
    assertTrue(NodeUniverseManager.fallsBackToSsh(CloudType.onprem));
  }

  @Test
  public void testEscapeYsqlQueryForShellPreservesSingleQuotedLiterals() {
    String escaped = NodeUniverseManager.escapeYsqlQueryForShell(QUERY_WITH_QUOTED_UUID);
    assertEquals(QUERY_WITH_QUOTED_UUID, escaped);
    assertFalse(escaped.contains("'\"'\"'"));
  }

  @Test
  public void testEscapeYsqlQueryForShellEscapesDoubleQuotesAndDollar() {
    String query = "SELECT \"$col\" FROM t WHERE v = '$val' AND n = ${name};";
    String escaped = NodeUniverseManager.escapeYsqlQueryForShell(query);
    assertEquals("SELECT \\\"\\$col\\\" FROM t WHERE v = '\\$val' AND n = \\${name};", escaped);
  }

  @Test
  public void testEscapeYsqlQueryForShellEscapesBackticksAndBackslashes() {
    // Backtick is command substitution and backslash can terminate the enclosing double quotes, so
    // both have to survive as literals rather than reaching the node's shell.
    assertEquals("SELECT \\`id\\`;", NodeUniverseManager.escapeYsqlQueryForShell("SELECT `id`;"));
    assertEquals("SELECT 'a\\\\b';", NodeUniverseManager.escapeYsqlQueryForShell("SELECT 'a\\b';"));
    // A trailing backslash must not escape the closing quote of the -c "..." wrapper.
    assertEquals("SELECT 1 \\\\\\\"", NodeUniverseManager.escapeYsqlQueryForShell("SELECT 1 \\\""));
  }

  @Test
  public void testYsqlBatchDbNameIsOneShellWord() {
    // The db name is joined into the bash -c string, so it must not be able to end the command.
    String dbName = "db; touch /tmp/pwned";
    RemoteBashRunSpec spec = ysqlSpec(dbName, "SELECT 1;", false);

    List<String> words = RemoteTransportSimulator.shellSplit(spec.command.get(2));
    assertEquals(dbName, words.get(words.indexOf("-d") + 1));
    assertFalse(words.contains("touch"));
    assertSurvivesAllTransports(spec.command, false);
  }

  @Test
  public void testBuildYsqlBatchRunSpecEscapesConsistencyCheckInsert() {
    UUID taskUuid = UUID.fromString("6263465a-70f8-4d83-a63c-bda81e11efa2");
    UUID ywUuid = UUID.fromString("e4d0ff57-3d5a-40da-b512-6cc1d8709d75");
    String insertQuery =
        String.format(
            "INSERT INTO %s (seq_num, task_uuid, yw_uuid, yw_host) VALUES (0, '%s', '%s', '%s')",
            CONSISTENCY_CHECK_TABLE_NAME, taskUuid, ywUuid, "dev-server-host");

    RemoteBashRunSpec spec = ysqlSpec("yugabyte", insertQuery, false);

    assertEquals("bash", spec.command.get(0));
    assertEquals("-c", spec.command.get(1));
    assertEquals(3, spec.command.size());

    String bashCommand = spec.command.get(2);
    assertFalse(bashCommand.startsWith("bash -c"));
    assertTrue(bashCommand.contains("/home/yugabyte/tserver/bin/ysqlsh"));
    assertTrue(bashCommand.contains("-c \""));
    assertFalse(bashCommand.contains("'\"'\"'"));
    assertTrue(bashCommand.contains("'" + taskUuid + "'"));
    assertTrue(bashCommand.contains("'" + ywUuid + "'"));
    assertTrue(bashCommand.contains("'dev-server-host'"));
  }

  @Test
  public void testRunYsqlBatchCommandsPassesUseBashFalse() {
    ShellResponse expected = new ShellResponse();
    expected.code = 0;
    doReturn(expected)
        .when(nodeUniverseManager)
        .runCommand(NodeUniverseContextMatchers.forNode(node, universe), anyList(), eq(false));

    ShellResponse response =
        nodeUniverseManager.runYsqlBatchCommands(
            node, universe, "yugabyte", List.of("SELECT 1;"), 300L, false, false);

    assertEquals(expected, response);
    verify(nodeUniverseManager)
        .runCommand(NodeUniverseContextMatchers.forNode(node, universe), anyList(), eq(false));
  }
}
