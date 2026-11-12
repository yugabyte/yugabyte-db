// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.cgroup;

import static com.yugabyte.yw.common.NodeUniverseContextMatchers.nodeContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseContext;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.NodeUniverseManager.RemoteBashRunSpec;
import com.yugabyte.yw.common.NodeUniverseYsqlContext;
import com.yugabyte.yw.common.RemoteTransportSimulator;
import com.yugabyte.yw.common.RemoteTransportSimulator.Transport;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestNodeUniverseManagers;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleNodeStaging;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleRunAsSupport;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleScriptEstimateSupport;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.CpuFallback;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleNodeBinarySupport;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.BashComponentSpec;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import org.apache.commons.io.FileUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;

/**
 * The executors that run support bundle scripts and commands from their staged copies, under the
 * limits script or not, and the bundle cleanup that removes the staging and stops leftover units.
 */
@RunWith(MockitoJUnitRunner.class)
public class SupportBundleCgroupExecutorTest extends FakeDBApplication {

  private static final String YB_HOME_DIR = "/home/yugabyte";
  private static final List<String> PARAMS =
      List.of("create_universe_logs_bundle", "--regex", "postgres .* log");

  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private RuntimeConfGetter mockConfGetter;

  private SupportBundleCgroupSupport cgroupSupport;
  private SupportBundleCgroupCommandExecutor commandExecutor;
  private SupportBundleCgroupScriptExecutor scriptExecutor;
  // A spy rather than the mock, for the tests that check what the real command building produces.
  private NodeUniverseManager nodeUniverseManager;
  private Universe universe;
  private final NodeDetails node = new NodeDetails();
  private Path localScriptPath;
  private UUID bundleUuid;
  private String stagingDir;
  private final List<List<String>> commandsRun = new ArrayList<>();
  private ShellResponse commandResponse;

  @Before
  public void setUp() throws Exception {
    cgroupSupport = new SupportBundleCgroupSupport(mockNodeUniverseManager, mockConfGetter);
    SupportBundleNodeStaging staging = new SupportBundleNodeStaging(mockNodeUniverseManager);
    commandExecutor =
        new SupportBundleCgroupCommandExecutor(mockNodeUniverseManager, cgroupSupport, staging);
    scriptExecutor =
        new SupportBundleCgroupScriptExecutor(
            mockNodeUniverseManager,
            cgroupSupport,
            new SupportBundleNodeBinarySupport(mockNodeUniverseManager),
            staging);
    nodeUniverseManager =
        TestNodeUniverseManagers.spyWith(
            app.injector().instanceOf(RuntimeConfGetter.class), YB_HOME_DIR);

    // shouldUseCgroup() reads the node's cluster, so the universe has to be persisted.
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
    bundleUuid = UUID.randomUUID();
    stagingDir = "/tmp/yba-support-bundle-" + bundleUuid + "-10_0_0_1";

    localScriptPath = Files.createTempFile("support_bundle_utils", ".sh");
    Files.writeString(localScriptPath, "#!/bin/bash\necho ok\n");
    commandResponse = ok("out\n" + trailer("quota"));
  }

  private void setLimitsEnabled(boolean enabled) {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled))
        .thenReturn(enabled);
    if (enabled) {
      when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupCpuPercent))
          .thenReturn(15);
      when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupMemorySoftPercent))
          .thenReturn(3);
      when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupMemoryHardPercent))
          .thenReturn(5);
    }
  }

  /** mkdir, rm and the unit stop succeed; any other command is recorded and gets the response. */
  private void stubRunCommand() {
    lenient().when(mockNodeUniverseManager.getYbHomeDir(any(), any())).thenReturn(YB_HOME_DIR);
    when(mockNodeUniverseManager.runCommand(any(NodeUniverseContext.class), anyList(), eq(false)))
        .thenAnswer(
            invocation -> {
              List<String> command = invocation.getArgument(1);
              if (command.get(0).equals("mkdir") || command.get(0).equals("rm")) {
                return ok("");
              }
              commandsRun.add(command);
              return commandResponse;
            });
  }

  private static ShellResponse ok(String message) {
    ShellResponse response = new ShellResponse();
    response.code = 0;
    response.message = message;
    return response;
  }

  private static String trailer(String cpuMode) {
    return "\nWRAPPER_LIMITS cpu_mode="
        + cpuMode
        + " cpu_max=15000/100000 memory_high=1 memory_max=2 unit=u reason=none\n";
  }

  private static ShellResponse cancelledResponse() {
    ShellResponse response = new ShellResponse();
    response.code = ShellResponse.ERROR_CODE_EXECUTION_CANCELLED;
    response.message = "Command is cancelled";
    return response;
  }

  private ShellResponse runScript(ShellProcessContext context, boolean applyLimits) {
    return scriptExecutor.executeScript(
        bundleUuid,
        universe,
        node,
        "bin/support_bundle_utils.sh",
        localScriptPath,
        PARAMS,
        context,
        ShellProcessContext.builder().timeoutSecs(60L).build(),
        "UniverseLogs",
        applyLimits);
  }

  private static ShellProcessContext context300() {
    return ShellProcessContext.builder().timeoutSecs(300L).build();
  }

  private String stagedScript() {
    return stagingDir + "/" + localScriptPath.getFileName();
  }

  private String stagedLimitsScript() {
    return stagingDir + "/" + SupportBundleV2LimitsExec.SCRIPT_NAME;
  }

  private static ListAppender<ILoggingEvent> capture(Class<?> type) {
    Logger logger = (Logger) LoggerFactory.getLogger(type);
    ListAppender<ILoggingEvent> appender = new ListAppender<>();
    appender.start();
    logger.addAppender(appender);
    return appender;
  }

  @Test
  public void testScriptRunsFromItsStagedCopyUnderTheLimitsScript() {
    setLimitsEnabled(true);
    stubRunCommand();

    ShellResponse response = runScript(context300(), true);

    List<String> argv = commandsRun.get(0);
    String unit = argv.get(argv.indexOf("--unit") + 1);
    List<String> expected = new ArrayList<>();
    expected.addAll(
        List.of(
            "bash",
            stagedLimitsScript(),
            "--cpu_percent",
            "15",
            "--memory_high_percent",
            "3",
            "--memory_max_percent",
            "5",
            "--runtime_max_secs",
            "330",
            "--unit",
            unit,
            "--slice",
            SupportBundleV2LimitsExec.SLICE,
            "--cpu_fallback",
            "freeze",
            "--exec",
            "bash",
            stagedScript(),
            "--yb_home_dir",
            YB_HOME_DIR,
            "--tmp_dir",
            stagingDir));
    expected.addAll(PARAMS);
    assertEquals(expected, argv);
    assertTrue(
        unit, unit.matches("yb-sb-" + bundleUuid.toString().substring(0, 8) + "-UniverseLogs-.+"));
    assertEquals("out\n", response.getMessage());
  }

  @Test
  public void testScriptRunsFromItsStagedCopyDirectlyWhenLimitsAreOff() {
    setLimitsEnabled(false);
    stubRunCommand();

    ShellResponse response = runScript(context300(), true);

    List<String> expected =
        new ArrayList<>(
            List.of(
                "bash",
                stagedScript(),
                "--run_timeout_secs",
                "330",
                "--yb_home_dir",
                YB_HOME_DIR,
                "--tmp_dir",
                stagingDir));
    expected.addAll(PARAMS);
    assertEquals(expected, commandsRun.get(0));
    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(any(NodeUniverseContext.class), anyString(), anyString(), anyString());
    // Not wrapped, so the output is returned as is.
    assertEquals(commandResponse.getMessage(), response.getMessage());
  }

  /** A command YBA doesn't time out gets no bound on the node either. */
  @Test
  public void testUntimedScriptGetsNoRunTimeout() {
    setLimitsEnabled(false);
    stubRunCommand();

    runScript(ShellProcessContext.builder().build(), true);

    List<String> expected =
        new ArrayList<>(
            List.of("bash", stagedScript(), "--yb_home_dir", YB_HOME_DIR, "--tmp_dir", stagingDir));
    expected.addAll(PARAMS);
    assertEquals(expected, commandsRun.get(0));
  }

  /** The staged script and the script's TMPDIR follow the node's configured tmp directory. */
  @Test
  public void testStagingFollowsTheNodesTmpDirectory() {
    setLimitsEnabled(false);
    stubRunCommand();
    when(mockNodeUniverseManager.getRemoteTmpDir(any(), any())).thenReturn("/data/tmp");

    runScript(context300(), true);

    String dir = "/data/tmp/yba-support-bundle-" + bundleUuid + "-10_0_0_1";
    List<String> argv = commandsRun.get(0);
    assertTrue(argv.toString(), argv.get(1).startsWith(dir + "/support_bundle_utils"));
    assertEquals(dir, argv.get(argv.indexOf("--tmp_dir") + 1));
  }

  @Test
  public void testEstimateNeverRunsUnderTheLimitsScript() {
    stubRunCommand();

    runScript(context300(), false);

    assertEquals(
        List.of("bash", stagedScript(), "--run_timeout_secs", "330"),
        commandsRun.get(0).subList(0, 4));
    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(any(NodeUniverseContext.class), anyString(), anyString(), anyString());
  }

  @Test
  public void testKubernetesStagesTheScriptButAppliesNoLimits() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)).thenReturn(true);
    Customer customer = Customer.get(universe.getCustomerId());
    universe =
        ModelFactory.createUniverse(
            "k8s-universe", UUID.randomUUID(), customer.getId(), CloudType.kubernetes);
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    stubRunCommand();

    runScript(context300(), true);

    assertEquals(
        List.of("bash", stagedScript(), "--run_timeout_secs", "330"),
        commandsRun.get(0).subList(0, 4));
    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(any(NodeUniverseContext.class), anyString(), anyString(), anyString());
  }

  @Test
  public void testAnotherUserGetsOnlyThePrioritySettingsAndStagesAsYugabyte() {
    setLimitsEnabled(true);
    stubRunCommand();
    ShellProcessContext asProviderUser =
        ShellProcessContext.builder().sshUser("ec2-user").timeoutSecs(300L).build();

    runScript(asProviderUser, true);

    List<String> argv = commandsRun.get(0);
    assertTrue(argv.toString(), argv.subList(0, argv.indexOf("--exec")).contains("--no_cgroup"));
    // It can't write the yugabyte-owned staging directory, so its TMPDIR stays /tmp.
    assertFalse(argv.toString(), argv.contains("--tmp_dir"));
    ArgumentCaptor<NodeUniverseContext> stageContext =
        ArgumentCaptor.forClass(NodeUniverseContext.class);
    verify(mockNodeUniverseManager, times(2))
        .uploadFileToNode(stageContext.capture(), anyString(), anyString(), eq("755"));
    stageContext
        .getAllValues()
        .forEach(
            context ->
                assertEquals(
                    ShellProcessContext.DEFAULT_REMOTE_USER,
                    context.getShellProcessContext().getSshUser()));
    verify(mockNodeUniverseManager)
        .runCommand(nodeContext(null, null, asProviderUser), eq(argv), eq(false));
  }

  @Test
  public void testExplicitYugabyteUserIsStillLimited() {
    setLimitsEnabled(true);
    stubRunCommand();

    runScript(ShellProcessContext.builder().sshUser("yugabyte").timeoutSecs(300L).build(), true);

    assertFalse(commandsRun.get(0).contains("--no_cgroup"));
    assertEquals(stagedLimitsScript(), commandsRun.get(0).get(1));
  }

  @Test
  public void testScriptsAreStagedOncePerBundleAndNode() {
    setLimitsEnabled(true);
    stubRunCommand();

    runScript(context300(), true);
    runScript(context300(), true);

    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(
            any(NodeUniverseContext.class), anyString(), eq(stagedScript()), eq("755"));
    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(
            any(NodeUniverseContext.class), anyString(), eq(stagedLimitsScript()), eq("755"));
    assertEquals(2, commandsRun.size());
  }

  @Test
  public void testScriptThatCantBeStagedFailsTheComponent() {
    when(mockNodeUniverseManager.runCommand(any(NodeUniverseContext.class), anyList(), eq(false)))
        .thenReturn(ok(""));
    doThrow(new RuntimeException("No space left on device"))
        .when(mockNodeUniverseManager)
        .uploadFileToNode(
            any(NodeUniverseContext.class), anyString(), eq(stagedScript()), anyString());

    RuntimeException thrown =
        assertThrows(RuntimeException.class, () -> runScript(context300(), true));

    assertTrue(thrown.getMessage(), thrown.getMessage().contains("No space left on device"));
    verify(mockNodeUniverseManager, never())
        .runCommand(
            any(NodeUniverseContext.class),
            argThat((List<String> cmd) -> cmd.size() > 1 && stagedScript().equals(cmd.get(1))),
            anyBoolean());
  }

  @Test
  public void testLimitsScriptThatCantBeStagedRunsTheCommandWithoutLimits() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)).thenReturn(true);
    stubRunCommand();
    doThrow(new RuntimeException("upload failed"))
        .when(mockNodeUniverseManager)
        .uploadFileToNode(
            any(NodeUniverseContext.class), anyString(), eq(stagedLimitsScript()), anyString());
    ListAppender<ILoggingEvent> logs = capture(SupportBundleCgroupScriptExecutor.class);

    runScript(context300(), true);

    assertEquals(
        List.of("bash", stagedScript(), "--run_timeout_secs", "330"),
        commandsRun.get(0).subList(0, 4));
    assertTrue(
        logs.list.stream()
            .anyMatch(e -> e.getFormattedMessage().contains("without limits: upload failed")));
  }

  /** A cancel while staging the limits script is an abort, not a reason to run unlimited. */
  @Test
  public void testCancelledLimitsStagingRunsNothing() {
    setLimitsEnabled(true);
    stubRunCommand();
    doThrow(new CancellationException("upload cancelled"))
        .when(mockNodeUniverseManager)
        .uploadFileToNode(
            any(NodeUniverseContext.class), anyString(), eq(stagedLimitsScript()), anyString());

    assertThrows(CancellationException.class, () -> runScript(context300(), true));

    assertTrue(Thread.interrupted());
    assertTrue(commandsRun.isEmpty());
  }

  /** Node agent's runCommand puts "Command output:" before the first line, with no newline. */
  @Test
  public void testNodeAgentOutputPrefixIsRemovedBeforeTheOutputIsRead() {
    setLimitsEnabled(true);
    commandResponse =
        ok(
            ShellResponse.RUN_COMMAND_OUTPUT_PREFIX
                + "BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/x.tgz"
                + trailer("quota"));
    stubRunCommand();

    ShellResponse response = runScript(context300(), true);

    assertEquals("BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/x.tgz", response.getMessage());
  }

  @Test
  public void testNodeAgentTrailerOnTheFirstLineIsRead() {
    setLimitsEnabled(true);
    commandResponse = ok(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + trailer("quota").substring(1));
    stubRunCommand();
    ListAppender<ILoggingEvent> logs = capture(SupportBundleCgroupSupport.class);

    ShellResponse response = runScript(context300(), true);

    assertEquals("", response.getMessage());
    assertTrue(
        logs.list.stream().anyMatch(e -> e.getFormattedMessage().contains("cpu_mode=quota")));
    assertFalse(
        logs.list.stream().anyMatch(e -> e.getFormattedMessage().contains("reported no limits")));
  }

  /** run_node_action.py prints the prefix on its own line, so SSH output keeps its lines. */
  @Test
  public void testSshOutputKeepsItsLines() {
    setLimitsEnabled(true);
    commandResponse =
        ok(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "\nBUNDLE_STATUS=OK\n" + trailer("quota"));
    stubRunCommand();

    ShellResponse response = runScript(context300(), true);

    assertEquals("\nBUNDLE_STATUS=OK\n", response.getMessage());
  }

  @Test
  public void testNodeAgentEstimateIsParsed() {
    commandResponse =
        ok(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "ESTIMATE_STATUS=OK\nESTIMATE_SIZE=123\n");
    stubRunCommand();
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleEstimateScriptTimeout))
        .thenReturn(Duration.ofMinutes(2));
    SupportBundleV2ManifestLoader emptyManifest = mock(SupportBundleV2ManifestLoader.class);
    when(emptyManifest.load()).thenReturn(new SupportBundleV2Manifest());
    SupportBundleScriptEstimateSupport estimateSupport =
        new SupportBundleScriptEstimateSupport(
            app.config(),
            mockConfGetter,
            mockNodeUniverseManager,
            new SupportBundleRunAsSupport(mock(ImageBundleUtil.class), emptyManifest),
            scriptExecutor,
            mock(ShellProcessHandler.class));
    BashComponentSpec spec = new BashComponentSpec();
    spec.setComponentName("test_member");
    spec.setScriptPath(localScriptPath.toString());
    spec.setParams(List.of("create_test_member_bundle"));

    long size =
        estimateSupport.runNodeScriptEstimate(
            spec,
            "test_member",
            new SupportBundleFormData(),
            universe,
            node,
            null,
            null,
            bundleUuid);

    assertEquals(123L, size);
  }

  @Test
  public void testRunLogsTheRunAndSetupUsers() {
    stubRunCommand();
    ListAppender<ILoggingEvent> logs = capture(SupportBundleCgroupScriptExecutor.class);

    scriptExecutor.executeScript(
        bundleUuid,
        universe,
        node,
        "bin/support_bundle_utils.sh",
        localScriptPath,
        PARAMS,
        ShellProcessContext.builder().sshUser("ec2-user").timeoutSecs(300L).build(),
        ShellProcessContext.builder().sshUser("yugabyte").timeoutSecs(60L).build(),
        "SystemLogs",
        false);

    assertTrue(
        logs.list.stream()
            .anyMatch(
                e ->
                    e.getLevel() == Level.INFO
                        && e.getFormattedMessage()
                            .equals(
                                "Running bin/support_bundle_utils.sh for component 'SystemLogs' on"
                                    + " node 'u-n1' as 'ec2-user' (setup as 'yugabyte')")));
  }

  @Test
  public void testMissingTrailerIsWarned() {
    setLimitsEnabled(true);
    commandResponse = ok("out\n");
    stubRunCommand();
    ListAppender<ILoggingEvent> logs = capture(SupportBundleCgroupSupport.class);

    ShellResponse response = runScript(context300(), true);

    assertEquals("out\n", response.getMessage());
    assertTrue(
        logs.list.stream().anyMatch(e -> e.getFormattedMessage().contains("reported no limits")));
  }

  @Test
  public void testCancelledScriptBecomesACancellationException() {
    setLimitsEnabled(false);
    commandResponse = cancelledResponse();
    stubRunCommand();

    assertThrows(CancellationException.class, () -> runScript(context300(), true));
  }

  @Test
  public void testYsqlBatchRunsUnderTheLimitsScriptWithoutFreeze() {
    setLimitsEnabled(true);
    RemoteBashRunSpec spec =
        new RemoteBashRunSpec(
            List.of("bash", "-c", "ysqlsh -c 'select 1'"),
            ShellProcessContext.builder()
                .timeoutSecs(30L)
                .logCmdOutput(true)
                .redactedVals(Map.of("secret", "REDACTED"))
                .build());
    when(mockNodeUniverseManager.buildYsqlSupportBundleBatchRunSpec(any(), any(), any()))
        .thenReturn(spec);
    stubRunCommand();

    ShellResponse response =
        commandExecutor.runYsqlQueries(
            bundleUuid, node, universe, "yugabyte", List.of("select 1"), 30L, "q");

    List<String> argv = commandsRun.get(0);
    assertEquals(stagedLimitsScript(), argv.get(1));
    assertEquals("none", argv.get(argv.indexOf("--cpu_fallback") + 1));
    assertEquals("60", argv.get(argv.indexOf("--runtime_max_secs") + 1));
    assertEquals(spec.command, argv.subList(argv.indexOf("--exec") + 1, argv.size()));
    ArgumentCaptor<NodeUniverseContext> context =
        ArgumentCaptor.forClass(NodeUniverseContext.class);
    verify(mockNodeUniverseManager).runCommand(context.capture(), eq(argv), eq(false));
    assertEquals(30L, context.getValue().getShellProcessContext().getTimeoutSecs());
    assertEquals(
        spec.context.getRedactedVals(),
        context.getValue().getShellProcessContext().getRedactedVals());
    assertEquals("out\n", response.getMessage());
  }

  @Test
  public void testYsqlBatchRunsDirectlyWhenLimitsAreOff() {
    setLimitsEnabled(false);
    when(mockNodeUniverseManager.runYsqlSupportBundleBatchCommands(any(), any(), anyList()))
        .thenReturn(ok("rows"));

    ShellResponse response =
        commandExecutor.runYsqlQueries(
            bundleUuid, node, universe, "yugabyte", List.of("select 1"), 30L, "q");

    assertEquals("rows", response.getMessage());
    verify(mockNodeUniverseManager, never())
        .uploadFileToNode(any(NodeUniverseContext.class), anyString(), anyString(), anyString());
  }

  @Test
  public void testYcqlBatchesShareOneStagedLimitsScript() {
    setLimitsEnabled(true);
    when(mockNodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(any(), any(), anyList()))
        .thenReturn(
            new RemoteBashRunSpec(
                List.of("bash", "-c", "ycqlsh"), ShellProcessContext.builder().build()));
    stubRunCommand();

    commandExecutor.runYcqlQueries(
        bundleUuid, node, universe, "system", List.of("select 1"), 30L, "c");
    commandExecutor.runYcqlQueries(
        bundleUuid, node, universe, "system", List.of("select 2"), 30L, "c");

    assertEquals(2, commandsRun.size());
    commandsRun.forEach(argv -> assertEquals(stagedLimitsScript(), argv.get(1)));
    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(any(NodeUniverseContext.class), anyString(), anyString(), anyString());
  }

  @Test
  public void testCommandFallsBackWhenTheLimitsScriptCantBeStaged() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)).thenReturn(true);
    when(mockNodeUniverseManager.buildYsqlSupportBundleBatchRunSpec(any(), any(), anyList()))
        .thenReturn(
            new RemoteBashRunSpec(
                List.of("bash", "-c", "ysqlsh"), ShellProcessContext.builder().build()));
    when(mockNodeUniverseManager.runCommand(any(NodeUniverseContext.class), anyList(), eq(false)))
        .thenReturn(ok(""));
    doThrow(new RuntimeException("upload failed"))
        .when(mockNodeUniverseManager)
        .uploadFileToNode(any(NodeUniverseContext.class), anyString(), anyString(), anyString());
    when(mockNodeUniverseManager.runYsqlSupportBundleBatchCommands(any(), any(), anyList()))
        .thenReturn(ok("rows"));

    ShellResponse response =
        commandExecutor.runYsqlQueries(
            bundleUuid, node, universe, "yugabyte", List.of("select 1"), 30L, "q");

    assertEquals("rows", response.getMessage());
  }

  @Test
  public void testCancelledLimitsStagingSkipsTheUnlimitedCommand() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)).thenReturn(true);
    when(mockNodeUniverseManager.buildYsqlSupportBundleBatchRunSpec(any(), any(), anyList()))
        .thenReturn(
            new RemoteBashRunSpec(
                List.of("bash", "-c", "ysqlsh"), ShellProcessContext.builder().build()));
    when(mockNodeUniverseManager.runCommand(any(NodeUniverseContext.class), anyList(), eq(false)))
        .thenReturn(cancelledResponse());

    assertThrows(
        CancellationException.class,
        () ->
            commandExecutor.runYsqlQueries(
                bundleUuid, node, universe, "yugabyte", List.of("select 1"), 30L, "q"));

    assertTrue(Thread.interrupted());
    verify(mockNodeUniverseManager, never())
        .runYsqlSupportBundleBatchCommands(any(), any(), anyList());
  }

  @Test
  public void testCancelledCommandBecomesACancellationException() {
    setLimitsEnabled(false);
    when(mockNodeUniverseManager.runYsqlSupportBundleBatchCommands(any(), any(), anyList()))
        .thenReturn(cancelledResponse());

    assertThrows(
        CancellationException.class,
        () ->
            commandExecutor.runYsqlQueries(
                bundleUuid, node, universe, "yugabyte", List.of("select 1"), 30L, "q"));
  }

  @Test
  public void testCleanupStopsUnitsWhereTheLimitsScriptRanAndRemovesTheStaging() {
    setLimitsEnabled(true);
    stubRunCommand();
    runScript(context300(), true);
    commandsRun.clear();

    scriptExecutor.cleanupBundle(bundleUuid, universe);

    assertEquals(1, commandsRun.size());
    String stop = commandsRun.get(0).get(2);
    assertTrue(
        stop,
        stop.contains(
            "systemctl --user stop 'yb-sb-" + bundleUuid.toString().substring(0, 8) + "-*.scope'"));
    verify(mockNodeUniverseManager)
        .runCommand(
            any(NodeUniverseContext.class), eq(List.of("rm", "-rf", stagingDir)), eq(false));
    // Closed: a collection thread still running after the abort can't stage and run again.
    assertThrows(CancellationException.class, () -> runScript(context300(), true));
    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(
            any(NodeUniverseContext.class), anyString(), eq(stagedScript()), eq("755"));
  }

  @Test
  public void testCleanupWithoutLimitsOnlyRemovesTheStaging() {
    setLimitsEnabled(false);
    stubRunCommand();
    runScript(context300(), true);
    commandsRun.clear();

    scriptExecutor.cleanupBundle(bundleUuid, universe);

    assertTrue(commandsRun.isEmpty());
    verify(mockNodeUniverseManager)
        .runCommand(
            any(NodeUniverseContext.class), eq(List.of("rm", "-rf", stagingDir)), eq(false));
  }

  @Test
  public void testCleanupOfAnUnstagedBundleTouchesNoNode() {
    scriptExecutor.cleanupBundle(bundleUuid, universe);

    verify(mockNodeUniverseManager, never())
        .runCommand(any(NodeUniverseContext.class), anyList(), eq(false));
  }

  /** Both argv shapes, plain and under the limits script, must reach the node intact. */
  private void assertSurvivesEveryTransport(List<String> command, CpuFallback cpuFallback) {
    List<String> wrapped =
        SupportBundleV2LimitsExec.argv(
            stagedLimitsScript(),
            new SupportBundleV2LimitsExec.Limits(
                15,
                3,
                5,
                330L,
                "yb-sb-12345678-x-abcdef",
                cpuFallback,
                false,
                SupportBundleV2LimitsExec.SLICE),
            command);
    for (Transport transport : Transport.values()) {
      assertEquals(
          "limits off, " + transport + " corrupted the command",
          command,
          RemoteTransportSimulator.deliverViaRunCommand(transport, command, false));
      assertEquals(
          "limits on, " + transport + " corrupted the wrapped command",
          wrapped,
          RemoteTransportSimulator.deliverViaRunCommand(transport, wrapped, false));
    }
  }

  @Test
  public void testScriptCommandSurvivesEveryTransport() {
    List<String> params =
        List.of("collect_files", "--start_date", "2026-08-01T00:00:00Z", "--regex", "a 'b' *");
    assertSurvivesEveryTransport(
        SupportBundleCgroupScriptExecutor.scriptCommand(
            stagedScript(), YB_HOME_DIR, null, params, 0L),
        CpuFallback.FREEZE);
    // The unwrapped form, with the node-side bound ahead of the entrypoint.
    List<String> bounded =
        SupportBundleCgroupScriptExecutor.scriptCommand(
            stagedScript(), YB_HOME_DIR, null, params, 330L);
    for (Transport transport : Transport.values()) {
      assertEquals(
          transport + " corrupted the bounded command",
          bounded,
          RemoteTransportSimulator.deliverViaRunCommand(transport, bounded, false));
    }
  }

  @Test
  public void testYsqlCommandSurvivesEveryTransport() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYsqlSupportBundleBatchRunSpec(
            NodeUniverseYsqlContext.of(universe, node).withTimeout(300L),
            "yugabyte",
            List.of(
                "SELECT datname FROM pg_database;",
                "SELECT * FROM pg_stat_activity WHERE state = 'active';"));
    assertSurvivesEveryTransport(spec.command, CpuFallback.NONE);
  }

  @Test
  public void testYcqlCommandSurvivesEveryTransport() {
    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            NodeUniverseYsqlContext.of(universe, node).withTimeout(300L),
            "",
            List.of(
                "SELECT keyspace_name FROM system_schema.keyspaces;",
                "SELECT * FROM system.local WHERE key = 'local';"));
    assertSurvivesEveryTransport(spec.command, CpuFallback.NONE);
  }

  @Test
  public void testYbAdminCommandSurvivesEveryTransport() {
    RemoteBashRunSpec spec =
        SupportBundleRemoteCommandBuilder.buildYbAdminSupportBundleBatchRunSpec(
            node,
            nodeUniverseManager,
            universe,
            List.of("list_all_masters", "get_universe_config"),
            List.of(),
            300L);
    assertSurvivesEveryTransport(spec.command, CpuFallback.NONE);
  }

  @Test
  public void testBuildYbAdminSupportBundleBatchRunSpecSingleCommand() {
    RemoteBashRunSpec spec =
        SupportBundleRemoteCommandBuilder.buildYbAdminSupportBundleBatchRunSpec(
            node, nodeUniverseManager, universe, List.of("list_all_masters"), List.of(), 300L);

    String bashCommand = spec.command.get(2);
    assertEquals("bash", spec.command.get(0));
    assertEquals("-c", spec.command.get(1));
    assertTrue(bashCommand.contains("printf '%s\\n' '-- command: list_all_masters'"));
    assertTrue(bashCommand.contains("/home/yugabyte/master/bin/yb-admin"));
    assertTrue(bashCommand.contains("list_all_masters"));
    assertTrue(bashCommand.contains("2>&1"));
    assertTrue(bashCommand.contains("(exit code $_ec)"));
    assertTrue(bashCommand.endsWith("exit 0"));
    assertEquals(300L, spec.context.getTimeoutSecs());
  }

  @Test
  public void testBuildYbAdminSupportBundleBatchRunSpecMultipleCommandsContinueOnFailure() {
    RemoteBashRunSpec spec =
        SupportBundleRemoteCommandBuilder.buildYbAdminSupportBundleBatchRunSpec(
            node,
            nodeUniverseManager,
            universe,
            List.of("list_all_masters", "list_all_tablet_servers", "invalid_command"),
            List.of(),
            300L);

    String bashCommand = spec.command.get(2);
    assertTrue(bashCommand.contains("-- command: list_all_masters"));
    assertTrue(bashCommand.contains("-- command: list_all_tablet_servers"));
    assertTrue(bashCommand.contains("-- command: invalid_command"));
    assertFalse(bashCommand.contains("&&"));
    assertTrue(bashCommand.endsWith("exit 0"));
  }

  /** A member's arguments follow each of its commands; bash runs the script against a stub. */
  @Test
  public void testYbAdminArgsFollowEachCommand() throws Exception {
    Path ybHome = Files.createTempDirectory("yb-home");
    try {
      Path ybAdmin = Files.createDirectories(ybHome.resolve("master/bin")).resolve("yb-admin");
      Files.writeString(ybAdmin, "#!/bin/bash\nprintf '<%s>' \"${@:3}\"; echo\n");
      assertTrue(ybAdmin.toFile().setExecutable(true));
      RemoteBashRunSpec spec =
          SupportBundleRemoteCommandBuilder.buildYbAdminSupportBundleBatchRunSpec(
              node,
              TestNodeUniverseManagers.spyWith(
                  app.injector().instanceOf(RuntimeConfGetter.class), ybHome.toString()),
              universe,
              List.of("list_tablets", "list_tables"),
              List.of("ysql.yugabyte", "order items", "0", "JSON"),
              300L);

      Process process = new ProcessBuilder(spec.command).redirectErrorStream(true).start();
      String output = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
      assertEquals(0, process.waitFor());
      assertEquals(
          "-- command: list_tablets\n"
              + "<-timeout_ms><300000><list_tablets><ysql.yugabyte><order items><0><JSON>\n\n"
              + "-- command: list_tables\n"
              + "<-timeout_ms><300000><list_tables><ysql.yugabyte><order items><0><JSON>\n\n",
          output);
      assertSurvivesEveryTransport(spec.command, CpuFallback.NONE);
    } finally {
      FileUtils.deleteDirectory(ybHome.toFile());
    }
  }

  @Test
  public void testBuildYbAdminSupportBundleBatchBashScriptIncludesSharedArgs() {
    String script =
        SupportBundleRemoteCommandBuilder.buildYbAdminSupportBundleBatchBashScript(
            node, nodeUniverseManager, universe, List.of("list_tables"), List.of("yugabyte"), 300L);

    assertTrue(script.contains("-- command: list_tables"));
    assertTrue(script.contains("list_tables"));
    assertTrue(script.contains("'yugabyte'"));
    assertTrue(script.contains("2>&1"));
  }
}
