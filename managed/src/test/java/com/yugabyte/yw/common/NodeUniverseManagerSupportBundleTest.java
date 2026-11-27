// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.common.NodeUniverseManager.RemoteBashRunSpec;
import com.yugabyte.yw.common.RemoteTransportSimulator.Transport;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

/**
 * The commands NodeUniverseManager builds for support bundle collection: ysqlsh and ycqlsh batches,
 * their credentials and timeouts, and what reaches the DB node.
 *
 * <p>The transport-parity tests assert those commands reach the node unchanged on all three
 * transports: node agent gRPC, Kubernetes, and SSH-only.
 *
 * <p>These commands are built once and then shipped over whichever transport the universe happens
 * to use, so a quoting scheme that suits one transport can silently corrupt SQL on another. That is
 * not visible from the Java side alone, which is why {@link RemoteTransportSimulator} models what
 * each transport delivers.
 */
@RunWith(MockitoJUnitRunner.class)
public class NodeUniverseManagerSupportBundleTest extends FakeDBApplication {

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
    node.yqlServerRpcPort = 9042;
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              u.setUniverseDetails(universeDetails);
            });
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

  @Test
  public void testYsqlSupportBundleBatchSurvivesAllTransports() {
    // Even a query with no quotes of its own is at risk, because the batch script frames every
    // query with printf '%s\n' '-- query: ...'.
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYsqlSupportBundleBatchRunSpec(
            node,
            universe,
            "yugabyte",
            List.of("SELECT datname FROM pg_database;", QUERY_WITH_QUOTED_UUID),
            300L,
            false,
            false);

    assertSurvivesAllTransports(spec.command, false);
  }

  @Test
  public void testYcqlSupportBundleQuotedLiteralSurvivesAllTransports() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node,
            universe,
            "system_schema",
            List.of("SELECT * FROM system_schema.tables WHERE keyspace_name = 'system';"),
            300L);

    assertSurvivesAllTransports(spec.command, false);
  }

  @Test
  public void testYcqlSupportBundleMultiQuerySurvivesAllTransports() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node,
            universe,
            "",
            List.of(
                "SELECT keyspace_name FROM system_schema.keyspaces",
                "SELECT * FROM system.local WHERE key = 'local'"),
            300L);

    assertSurvivesAllTransports(spec.command, false);
  }

  @Test
  public void testYcqlSupportBundleBatchSurvivesAllTransports() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node,
            universe,
            "",
            List.of("SELECT keyspace_name FROM system_schema.keyspaces;"),
            300L);

    assertSurvivesAllTransports(spec.command, false);
  }

  @Test
  public void testBuildYcqlSupportBundleBatchRunSpecSingleQuery() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node,
            universe,
            null,
            List.of("SELECT keyspace_name FROM system_schema.keyspaces;"),
            300L);

    String bashCommand = spec.command.get(2);
    assertEquals("bash", spec.command.get(0));
    assertEquals("-c", spec.command.get(1));
    assertTrue(
        bashCommand.contains(
            "printf '%s\\n' '-- query: SELECT keyspace_name FROM system_schema.keyspaces;'"));
    assertTrue(bashCommand.contains("/home/yugabyte/tserver/bin/ycqlsh"));
    assertTrue(bashCommand.contains("10.0.0.1"));
    assertTrue(bashCommand.contains("9042"));
    assertTrue(bashCommand.contains("-e"));
    assertTrue(bashCommand.contains("2>&1"));
    assertTrue(bashCommand.contains("(exit code $_ec)"));
    assertTrue(bashCommand.endsWith("exit 0"));
    assertEquals(300L, spec.context.getTimeoutSecs());
  }

  @Test
  public void testBuildYcqlSupportBundleBatchRunSpecMultipleQueriesContinueOnFailure() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node,
            universe,
            "my_ks",
            List.of(
                "SELECT * FROM system.local;",
                "SELECT invalid cql;",
                "SELECT keyspace_name FROM system_schema.keyspaces;"),
            300L);

    String bashCommand = spec.command.get(2);
    assertTrue(bashCommand.contains("-- query: SELECT * FROM system.local;"));
    assertTrue(bashCommand.contains("-- query: SELECT invalid cql;"));
    assertTrue(
        bashCommand.contains("-- query: SELECT keyspace_name FROM system_schema.keyspaces;"));
    assertTrue(bashCommand.contains("-k 'my_ks'"));
    assertFalse(bashCommand.contains("&&"));
    assertTrue(bashCommand.endsWith("exit 0"));
  }

  @Test
  public void testYcqlSupportBundleKeyspaceIsOneShellWord() {
    // The keyspace is joined into the bash -c script, so it must not be able to end the command.
    String keyspace = "ks; touch /tmp/pwned";
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node, universe, keyspace, List.of("SELECT * FROM system.local;"), 300L);

    List<String> words = RemoteTransportSimulator.shellSplit(spec.command.get(2));
    assertEquals(keyspace, words.get(words.indexOf("-k") + 1));
    assertFalse(words.contains("touch"));
    assertSurvivesAllTransports(spec.command, false);
  }

  /** YBA has no current YCQL password, so even a stored create-time one is never sent. */
  @Test
  public void testBuildYcqlSupportBundleBatchRunSpecNeverPassesCredentials() {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().userIntent.enableYCQLAuth = true;
              details.getPrimaryCluster().userIntent.ycqlPassword = "secret-pass";
              u.setUniverseDetails(details);
            });

    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node, universe, null, List.of("SELECT * FROM system.local;"), 300L);

    String bashCommand = spec.command.get(2);
    assertFalse(bashCommand.contains("-u "));
    assertFalse(bashCommand.contains("-p "));
    assertFalse(bashCommand.contains("secret-pass"));
    assertTrue(spec.context.isLogCmdOutput());
  }

  @Test
  public void testBuildYcqlSupportBundleBatchBashScriptEscapesQuotes() {
    String script =
        nodeUniverseManager.buildYcqlSupportBundleBatchBashScript(
            node, universe, null, List.of("SELECT \"$col\" FROM t WHERE v = '$val';"));

    assertTrue(script.contains("printf '%s\\n'"));
    assertTrue(script.contains("-- query: SELECT"));
    assertTrue(script.contains("-e \""));
    assertTrue(script.contains("2>&1"));
    // The query is embedded in ycqlsh -e "...", so $ must stay escaped or bash expands it.
    assertTrue(script.contains("SELECT \\\"\\$col\\\" FROM t WHERE v = '\\$val';"));
  }

  @Test
  public void testBuildYcqlSupportBundleBatchBashScriptNeutralizesCommandSubstitution() {
    // Backtick is command substitution and backslash can terminate the enclosing double quotes, so
    // both have to survive as literals rather than reaching the node's shell.
    String script =
        nodeUniverseManager.buildYcqlSupportBundleBatchBashScript(
            node, universe, null, List.of("SELECT `id` FROM t WHERE v = 'a\\b';"));

    assertTrue(script.contains("-e \"SELECT \\`id\\` FROM t WHERE v = 'a\\\\b';\""));
    assertFalse(script.contains("-e \"SELECT `id`"));
  }

  @Test
  public void testBuildYcqlSupportBundleBatchRunSpecClientToNodeEncryption() {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().userIntent.enableClientToNodeEncrypt = true;
              u.setUniverseDetails(details);
            });

    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node, universe, null, List.of("SELECT * FROM system.local;"), 300L);

    assertTrue(spec.command.get(2).contains("--ssl"));
  }

  @Test
  public void testBuildYcqlSupportBundleBatchRunSpecEscapesQuotesAndDollar() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node,
            universe,
            null,
            List.of("SELECT \"$col\" FROM t WHERE v = '$val' AND x = $1;"),
            300L);

    String bashCommand = spec.command.get(2);
    // Single quotes stay literal; the '"'"' idiom would corrupt CQL string literals here.
    assertTrue(
        bashCommand.contains("SELECT \\\"\\$col\\\" FROM t WHERE v = '\\$val' AND x = \\$1;"));
  }

  @Test
  public void testBuildYsqlSupportBundleBatchRunSpecSingleQuery() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYsqlSupportBundleBatchRunSpec(
            node,
            universe,
            "yugabyte",
            List.of("SELECT datname FROM pg_database;"),
            300L,
            false,
            false);

    String bashCommand = spec.command.get(2);
    assertEquals("bash", spec.command.get(0));
    assertEquals("-c", spec.command.get(1));
    assertTrue(bashCommand.contains("printf '%s\\n' '-- query: SELECT datname FROM pg_database;'"));
    assertTrue(bashCommand.contains("/home/yugabyte/tserver/bin/ysqlsh"));
    assertTrue(bashCommand.contains("10.0.0.1"));
    assertTrue(bashCommand.contains("5433"));
    assertTrue(bashCommand.contains("2>&1"));
    assertTrue(bashCommand.contains("(exit code $_ec)"));
    assertTrue(bashCommand.endsWith("exit 0"));
    assertEquals(300L, spec.context.getTimeoutSecs());
  }

  @Test
  public void testBuildYsqlSupportBundleBatchRunSpecMultipleQueriesContinueOnFailure() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYsqlSupportBundleBatchRunSpec(
            node,
            universe,
            "yugabyte",
            List.of("SELECT 1;", "SELECT bad_syntax;", "SELECT 3;"),
            300L,
            false,
            false);

    String bashCommand = spec.command.get(2);
    assertTrue(bashCommand.contains("-- query: SELECT 1;"));
    assertTrue(bashCommand.contains("-- query: SELECT bad_syntax;"));
    assertTrue(bashCommand.contains("-- query: SELECT 3;"));
    assertFalse(bashCommand.contains("&&"));
    assertTrue(bashCommand.endsWith("exit 0"));
  }

  @Test
  public void testBuildYsqlSupportBundleBatchBashScriptEscapesQuotes() {
    String script =
        nodeUniverseManager.buildYsqlSupportBundleBatchBashScript(
            node,
            universe,
            "yugabyte",
            List.of("SELECT 'value' FROM t WHERE col = \"x\";"),
            false,
            false);

    assertTrue(script.contains("printf '%s\\n'"));
    assertTrue(script.contains("-- query: SELECT"));
    assertTrue(script.contains("FROM t WHERE col"));
    assertTrue(script.contains("2>&1"));
    assertTrue(script.contains("-c \""));
    assertTrue(script.contains("SELECT 'value' FROM t WHERE col = \\\"x\\\";"));
  }

  @Test
  public void testBuildYsqlSupportBundleBatchBashScriptNeutralizesCommandSubstitution() {
    String script =
        nodeUniverseManager.buildYsqlSupportBundleBatchBashScript(
            node, universe, "yugabyte", List.of("SELECT `id`;"), false, false);

    assertTrue(script.contains("-c \"SELECT \\`id\\`;\""));
    assertFalse(script.contains("-c \"SELECT `id`;\""));
  }

  @Test
  public void testBuildYsqlSupportBundleBatchBashScriptForcesReadOnlySession() {
    String script =
        nodeUniverseManager.buildYsqlSupportBundleBatchBashScript(
            node, universe, "yugabyte", List.of("SELECT 1;"), false, false);

    assertTrue(script.contains("export PGOPTIONS='-c default_transaction_read_only=on';"));
  }

  /** With pooling on, Connection Manager owns the YSQL port, so PGOPTIONS must go to Postgres. */
  @Test
  public void testYsqlSupportBundleBatchBypassesConnectionManager() {
    node.internalYsqlServerRpcPort = 6433;
    String script =
        nodeUniverseManager.buildYsqlSupportBundleBatchBashScript(
            node, universe, "yugabyte", List.of("SELECT 1;"), false, true);

    assertTrue(script.contains(" -p 6433 "));
    assertFalse(script.contains(" -p 5433 "));
    assertTrue(script.contains("export PGOPTIONS='-c default_transaction_read_only=on';"));
  }

  @Test
  public void testBuildYsqlBatchRunSpecDoesNotForceReadOnlySession() {
    // The shared builder also serves writers such as UpdateConsistencyCheck.
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYsqlBatchRunSpec(
            node, universe, "yugabyte", List.of("SELECT 1;"), 300L, false, false, true);

    assertFalse(spec.command.get(2).contains("default_transaction_read_only"));
  }

  @Test
  public void testRunYsqlSupportBundleBatchCommandsPassesUseBashFalse() {
    ShellResponse expected = new ShellResponse();
    expected.code = 0;
    doReturn(expected)
        .when(nodeUniverseManager)
        .runCommand(NodeUniverseContextMatchers.forNode(node, universe), anyList(), eq(false));

    ShellResponse response =
        nodeUniverseManager.runYsqlSupportBundleBatchCommands(
            node,
            universe,
            "yugabyte",
            List.of("SELECT datname FROM pg_database;"),
            300L,
            false,
            false);

    assertEquals(expected, response);
    verify(nodeUniverseManager)
        .runCommand(NodeUniverseContextMatchers.forNode(node, universe), anyList(), eq(false));
  }
}
