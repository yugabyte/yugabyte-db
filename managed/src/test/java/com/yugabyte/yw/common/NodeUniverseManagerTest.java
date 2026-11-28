// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
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
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
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

  @Rule public TemporaryFolder tmp = new TemporaryFolder();

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
  public void testDownloadRootFilesToleratesFileChangedDuringRead() {
    Universe universe =
        ModelFactory.createFromConfig(defaultProvider, "root-files-test", "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));

    nodeUniverseManager.downloadRootFiles(
        UUID.randomUUID(), node, universe, Set.of("/var/log/messages"), "/tmp/syslogs.tar.gz");

    List<String> expected = new ArrayList<>(List.of("sudo"));
    expected.addAll(
        NodeUniverseManager.tarToleratingFileChanged(
            List.of("-czhf", "/tmp/root_files.tar.gz", "/var/log/messages")));
    verify(shellProcessHandler, atLeastOnce())
        .run(commandCaptor.capture(), any(ShellProcessContext.class));
    assertTrue(
        "sent: " + commandCaptor.getAllValues(),
        commandCaptor.getAllValues().stream()
            .anyMatch(command -> Collections.indexOfSubList(command, expected) >= 0));
  }

  @Test
  public void testTarToleratingFileChangedMapsOnlyExitOne() throws Exception {
    // GNU tar exits 1 when a file changed while read, 2 on a fatal error such as a missing file.
    assertEquals(0, runWithStubTar(0));
    assertEquals(0, runWithStubTar(1));
    assertEquals(2, runWithStubTar(2));
    assertEquals(
        List.of("--warning=no-file-changed", "-czhf", "/tmp/a.tar.gz", "/var/log/a b.log"),
        Files.readAllLines(tmp.getRoot().toPath().resolve("tar-args")));
  }

  @Test
  public void testRunNodeActionTarScriptMatchesJava() throws Exception {
    String python = Files.readString(Paths.get("devops/bin/run_node_action.py"));
    assertTrue(python.contains("'" + NodeUniverseManager.TAR_TOLERATING_FILE_CHANGED_SCRIPT + "'"));
  }

  /** Runs the wrapped tar through real bash, with a stub tar that exits {@code tarExitCode}. */
  private int runWithStubTar(int tarExitCode) throws Exception {
    Path stubTar = tmp.getRoot().toPath().resolve("bin/tar");
    Files.createDirectories(stubTar.getParent());
    Files.writeString(
        stubTar,
        String.format(
            "#!/bin/bash\nprintf '%%s\\n' \"$@\" > \"%s\"\nexit %d\n",
            tmp.getRoot().toPath().resolve("tar-args"), tarExitCode));
    assertTrue(stubTar.toFile().setExecutable(true));
    ProcessBuilder pb =
        new ProcessBuilder(
            NodeUniverseManager.tarToleratingFileChanged(
                List.of("-czhf", "/tmp/a.tar.gz", "/var/log/a b.log")));
    pb.environment().put("PATH", stubTar.getParent() + File.pathSeparator + System.getenv("PATH"));
    pb.redirectErrorStream(true);
    Process process = pb.start();
    process.getInputStream().readAllBytes();
    assertTrue(process.waitFor(30, TimeUnit.SECONDS));
    return process.exitValue();
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

  /** ManageCatalogUpgradeSuperUser's query: the password is doubled inside a format() string. */
  @Test
  public void testRunYsqlBatchCommandsRedactsFormatStringPasswordOverSsh() {
    String logged =
        runYsqlBatchOverSsh(
            "format-password-test",
            "DO $$DECLARE time TIMESTAMP := now() + INTERVAL '4 HOURS';\nBEGIN\n"
                + "EXECUTE format('CREATE ROLE yugabyte_upgrade WITH SUPERUSER LOGIN PASSWORD"
                + " ''Ab1!@^*zQ9'' VALID UNTIL ''%s'';', time);\nEND$$;\n",
            true);

    assertTrue(logged, logged.contains("PASSWORD REDACTED VALID UNTIL ''%s''"));
    assertFalse(logged, logged.contains("Ab1!@^*zQ9"));
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
