// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.NodeAgentPoller;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.Silent.class)
public class NodeUniverseManagerTest extends FakeDBApplication {

  @InjectMocks NodeUniverseManager nodeUniverseManager;

  @Mock ShellProcessHandler shellProcessHandler;
  @Mock RuntimeConfGetter mockConfGetter;
  @Mock LocalNodeUniverseManager localNodeUniverseManager;
  @Mock NodeActionRunner nodeActionRunner;
  @Mock ImageBundleUtil imageBundleUtil;
  @Mock NodeAgentPoller nodeAgentPoller;
  @Mock NodeAgentClient nodeAgentClient;
  @Mock FileHelperService fileHelperService;

  private Provider defaultProvider;
  private ArgumentCaptor<List<String>> commandCaptor;
  private ArgumentCaptor<ShellProcessContext> shellProcessContextCaptor;

  @Before
  public void setUp() {
    defaultProvider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    AccessKey.create(defaultProvider.getUuid(), "demo-access", new AccessKey.KeyInfo());
    commandCaptor = ArgumentCaptor.forClass(List.class);
    shellProcessContextCaptor = ArgumentCaptor.forClass(ShellProcessContext.class);
    when(nodeAgentClient.maybeGetAndUpgrade(any())).thenReturn(Optional.empty());
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ssh2Enabled))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.devopsCommandTimeout)))
        .thenReturn(Duration.ofHours(1));
    when(mockConfGetter.getConfForScope(
            any(Provider.class), eq(ProviderConfKeys.remoteTmpDirectory)))
        .thenReturn("/tmp");
  }

  @Test
  public void testRunYsqlBatchCommandsDisablesCommandLogging() {
    Universe universe =
        ModelFactory.createFromConfig(defaultProvider, "slow-query-test", "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    String sensitiveQuery = "SELECT query, calls FROM pg_stat_statements WHERE query LIKE '%ssn%'";

    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));

    nodeUniverseManager.runYsqlBatchCommands(
        node, universe, "postgres", List.of(sensitiveQuery), 180L, false, false, false);

    verify(shellProcessHandler).run(commandCaptor.capture(), shellProcessContextCaptor.capture());
    ShellProcessContext context = shellProcessContextCaptor.getValue();
    assertFalse(context.isLogCmdOutput());
    assertTrue(context.getRedactedVals().containsValue(Util.REDACTED_YSQL_QUERY));
    assertTrue(
        context.getRedactedVals().keySet().stream()
            .anyMatch(val -> val.contains("pg_stat_statements")));
  }

  @Test
  public void testRunYsqlBatchCommandsLogsCommandByDefault() {
    Universe universe =
        ModelFactory.createFromConfig(defaultProvider, "slow-query-test-2", "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    String query = "SELECT 1";

    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));

    nodeUniverseManager.runYsqlBatchCommands(
        node, universe, "postgres", List.of(query), 180L, false, false, true);

    verify(shellProcessHandler).run(commandCaptor.capture(), shellProcessContextCaptor.capture());
    ShellProcessContext context = shellProcessContextCaptor.getValue();
    assertTrue(context.isLogCmdOutput());
    assertTrue(context.getRedactedVals().isEmpty());
  }

  @Test
  public void testGetYsqlSocketDirExpressionMatchesPort() {
    assertEquals(
        "$(dirname \"$(ls -t /tmp/.yb.*/.s.PGSQL.6970 2>/dev/null | head -1)\")",
        NodeUniverseManager.getYsqlSocketDirExpression("/tmp", "6970"));
    // Postgres falls back to /tmp when tmp_dir is too long.
    assertEquals(
        "$(dirname \"$(ls -t /home/yugabyte/tmp/.yb.*/.s.PGSQL.6970"
            + " /tmp/.yb.*/.s.PGSQL.6970 2>/dev/null | head -1)\")",
        NodeUniverseManager.getYsqlSocketDirExpression("/home/yugabyte/tmp", "6970"));
  }

  @Test
  public void testRunYsqlBatchCommandsPicksSocketForConnectedPort() {
    Universe universe =
        ModelFactory.createFromConfig(defaultProvider, "socket-port-test", "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    node.ysqlServerRpcPort = 6969;
    node.internalYsqlServerRpcPort = 6970;

    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));

    // With connection pooling, postgres listens on the internal port.
    nodeUniverseManager.runYsqlBatchCommands(
        node, universe, "postgres", List.of("SELECT 1"), 180L, true, true);
    nodeUniverseManager.runYsqlBatchCommands(
        node, universe, "postgres", List.of("SELECT 1"), 180L, true, false);

    verify(shellProcessHandler, times(2))
        .run(commandCaptor.capture(), any(ShellProcessContext.class));
    String cpCommand = String.join(" ", commandCaptor.getAllValues().get(0));
    assertTrue(cpCommand, cpCommand.contains("/tmp/.yb.*/.s.PGSQL.6970 2>/dev/null"));
    assertTrue(cpCommand, cpCommand.contains(" -p 6970 "));
    String command = String.join(" ", commandCaptor.getAllValues().get(1));
    assertTrue(command, command.contains("/tmp/.yb.*/.s.PGSQL.6969 2>/dev/null"));
    assertTrue(command, command.contains(" -p 6969 "));
    assertFalse(command, command.contains(".s.PGSQL.*"));
  }

  /** On SSH the argv is escaped after the context is built; the redaction must still match. */
  @Test
  public void testRunYsqlBatchCommandsRedactsPasswordOverSsh() {
    String logged =
        runYsqlBatchOverSsh(
            "admin-password-test",
            "ALTER USER \"yugabyte\" WITH PASSWORD 'S3cr3t-pw'; SELECT pg_stat_statements_reset();",
            true);

    assertTrue(logged.contains("PASSWORD"));
    assertFalse(logged.contains("S3cr3t-pw"));
  }

  /** A secret with a shell character is quoted for ssh.py; the redaction must match that form. */
  @Test
  public void testRunCommandRedactsQuotedSecretOverSsh() {
    Universe universe =
        ModelFactory.createFromConfig(defaultProvider, "quoted-secret-test", "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));
    String secret = "--password=pa$$w0rd";
    ShellProcessContext context =
        ShellProcessContext.builder().redactedVals(Map.of(secret, "--password=REDACTED")).build();

    nodeUniverseManager.runCommand(node, universe, List.of("/bin/tool", secret), context, false);

    verify(shellProcessHandler).run(commandCaptor.capture(), shellProcessContextCaptor.capture());
    List<String> sent = commandCaptor.getValue();
    assertTrue(sent.contains("'" + secret + "'"));
    String logged =
        String.join(" ", shellProcessContextCaptor.getValue().redactCommand(new ArrayList<>(sent)));
    assertTrue(logged.contains("--password=REDACTED"));
    assertFalse(logged.contains("w0rd"));
  }

  @Test
  public void testRunYsqlBatchCommandsRedactsQueryOverSsh() {
    String logged =
        runYsqlBatchOverSsh(
            "redacted-query-test",
            "SELECT query FROM pg_stat_statements WHERE query LIKE '%ssn%'",
            false);

    assertTrue(logged.contains(Util.REDACTED_YSQL_QUERY));
    assertFalse(logged.contains("ssn"));
  }

  /** run_query passes user SQL through, which may end without ';' or use lowercase. */
  @Test
  public void testRunYsqlBatchCommandsRedactsUnterminatedPasswordOverSsh() {
    String logged =
        runYsqlBatchOverSsh(
            "unterminated-password-test", "create user s_live password 'S3cr3t pw$1'", true);

    assertTrue(logged.contains("PASSWORD REDACTED"));
    assertFalse(logged.contains("S3cr3t"));
  }

  /** run_node_action.py joins params with spaces for the node's shell, so each is quoted. */
  @Test
  public void testRunScriptQuotesParamsOverSsh() {
    Universe universe =
        ModelFactory.createFromConfig(defaultProvider, "run-script-test", "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));

    nodeUniverseManager.runScript(
        node, universe, "/tmp/script.sh", List.of("collect_files", "postgres .* log", "it's"));

    verify(shellProcessHandler).run(commandCaptor.capture(), any(ShellProcessContext.class));
    List<String> argv = commandCaptor.getValue();
    assertEquals(
        List.of("'collect_files'", "'postgres .* log'", "'it'\\''s'"),
        argv.subList(argv.indexOf("--params") + 1, argv.size()));
  }

  /** Returns the argv as ShellProcessHandler logs it. */
  private String runYsqlBatchOverSsh(String universeName, String query, boolean logCmdOutput) {
    Universe universe = ModelFactory.createFromConfig(defaultProvider, universeName, "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));

    nodeUniverseManager.runYsqlBatchCommands(
        node, universe, "yugabyte", List.of(query), 180L, false, false, logCmdOutput);

    verify(shellProcessHandler).run(commandCaptor.capture(), shellProcessContextCaptor.capture());
    List<String> sent = commandCaptor.getValue();
    // The query is really sent, escaped for ssh.py; only the log copy is redacted.
    assertTrue(String.join(" ", sent).contains("'\"'\"'"));
    return String.join(
        " ", shellProcessContextCaptor.getValue().redactCommand(new ArrayList<>(sent)));
  }
}
