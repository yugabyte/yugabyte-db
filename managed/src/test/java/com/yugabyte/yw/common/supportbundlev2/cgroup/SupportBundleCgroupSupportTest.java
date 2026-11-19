// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.cgroup;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.CpuFallback;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.Limits;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;

/** Which commands get limits, which limits, and how the script's report is logged and stripped. */
@RunWith(MockitoJUnitRunner.class)
public class SupportBundleCgroupSupportTest {

  private static final UUID BUNDLE_UUID = UUID.fromString("33333333-4444-5555-6666-777777777777");

  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private RuntimeConfGetter mockConfGetter;
  @Mock private Universe universe;

  private final NodeDetails node = new NodeDetails();
  private SupportBundleCgroupSupport cgroupSupport;
  private ListAppender<ILoggingEvent> logs;

  @Before
  public void setUp() {
    cgroupSupport = new SupportBundleCgroupSupport(mockNodeUniverseManager, mockConfGetter);
    node.nodeName = "u-n1";
    Logger logger = (Logger) LoggerFactory.getLogger(SupportBundleCgroupSupport.class);
    logs = new ListAppender<>();
    logs.start();
    logger.addAppender(logs);
  }

  private void stubPercentages(int cpu, int memorySoft, int memoryHard) {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupCpuPercent))
        .thenReturn(cpu);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupMemorySoftPercent))
        .thenReturn(memorySoft);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupMemoryHardPercent))
        .thenReturn(memoryHard);
  }

  private long warnings(String text) {
    return logs.list.stream()
        .filter(e -> e.getLevel() == Level.WARN && e.getFormattedMessage().contains(text))
        .count();
  }

  private static ShellResponse ok(String message) {
    ShellResponse response = new ShellResponse();
    response.code = 0;
    response.message = message;
    return response;
  }

  private static String trailer(String cpuMode, String reason) {
    return "out\n\nWRAPPER_LIMITS cpu_mode="
        + cpuMode
        + " cpu_max=none memory_high=1 memory_max=2 unit=u reason="
        + reason
        + "\n";
  }

  @Test
  public void testShouldUseCgroupIsFalseWhenDisabled() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)).thenReturn(false);
    assertFalse(cgroupSupport.shouldUseCgroup(universe, node));
  }

  @Test
  public void testShouldUseCgroupOnEveryVmNodeProvisionedOrNot() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)).thenReturn(true);
    when(universe.getNodeDeploymentMode(node)).thenReturn(CloudType.aws, CloudType.onprem);
    when(universe.getUniverseUUID()).thenReturn(UUID.randomUUID());
    assertTrue(cgroupSupport.shouldUseCgroup(universe, node));
    assertTrue(cgroupSupport.shouldUseCgroup(universe, node));
  }

  @Test
  public void testShouldUseCgroupIsFalseOnKubernetes() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)).thenReturn(true);
    when(universe.getNodeDeploymentMode(node)).thenReturn(CloudType.kubernetes);
    when(universe.getUniverseUUID()).thenReturn(UUID.randomUUID());
    assertFalse(cgroupSupport.shouldUseCgroup(universe, node));
  }

  @Test
  public void testUnknownCloudTypeRunsWithoutLimitsAndWarnsOncePerUniverse() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)).thenReturn(true);
    when(universe.getNodeDeploymentMode(node)).thenReturn(null);
    when(universe.getUniverseUUID()).thenReturn(UUID.randomUUID());

    assertFalse(cgroupSupport.shouldUseCgroup(universe, node));
    assertFalse(cgroupSupport.shouldUseCgroup(universe, node));

    assertEquals(1, warnings("cloud type is unknown"));
  }

  @Test
  public void testGetLimitsClampsTheConfiguredPercentages() {
    stubPercentages(0, 120, 150);
    SupportBundleCgroupSupport.CgroupLimits limits = cgroupSupport.getLimits();
    assertEquals(15, limits.cpuPercent);
    assertEquals(100, limits.memorySoftPercent);
    assertEquals(100, limits.memoryHardPercent);

    stubPercentages(150, 8, 5);
    limits = cgroupSupport.getLimits();
    assertEquals(100, limits.cpuPercent);
    assertEquals(5, limits.memorySoftPercent);
    assertEquals(5, limits.memoryHardPercent);
  }

  @Test
  public void testGetLimitsUsesTheDefaultForANegativeMemoryPercentage() {
    stubPercentages(15, -1, -2);
    SupportBundleCgroupSupport.CgroupLimits limits = cgroupSupport.getLimits();
    assertEquals(3, limits.memorySoftPercent);
    assertEquals(5, limits.memoryHardPercent);
    assertEquals(1, warnings("memory_soft_percent (-1) is negative"));
    assertEquals(1, warnings("memory_hard_percent (-2) is negative"));

    // Zero still turns the cap off.
    stubPercentages(15, 0, 0);
    limits = cgroupSupport.getLimits();
    assertEquals(0, limits.memorySoftPercent);
    assertEquals(0, limits.memoryHardPercent);
  }

  @Test
  public void testLimitsForAddsTheMarginToTheTimeout() {
    stubPercentages(15, 3, 5);

    Limits limits =
        cgroupSupport.limitsFor(BUNDLE_UUID, "UniverseLogs", 300L, CpuFallback.FREEZE, true);
    assertEquals(15, limits.cpuPercent());
    assertEquals(3, limits.memoryHighPercent());
    assertEquals(5, limits.memoryMaxPercent());
    assertEquals(330L, limits.runtimeMaxSecs());
    assertEquals(CpuFallback.FREEZE, limits.cpuFallback());
    assertTrue(limits.noCgroup());
    assertEquals(SupportBundleV2LimitsExec.SLICE, limits.sliceName());

    assertEquals(
        0L,
        cgroupSupport.limitsFor(BUNDLE_UUID, "q", 0L, CpuFallback.NONE, false).runtimeMaxSecs());
  }

  @Test
  public void testUnitNamesShareTheBundlePrefix() {
    String prefix = SupportBundleCgroupSupport.unitPrefix(BUNDLE_UUID);
    assertEquals("yb-sb-33333333-", prefix);

    String first = SupportBundleCgroupSupport.unitName(BUNDLE_UUID, "Universe Logs/1");
    String second = SupportBundleCgroupSupport.unitName(BUNDLE_UUID, "Universe Logs/1");
    assertTrue(first, first.matches("yb-sb-33333333-Universe_Logs_1-[0-9a-f]{6}"));
    assertNotEquals(first, second);
    assertTrue(
        SupportBundleCgroupSupport.unitName(BUNDLE_UUID, " ").startsWith(prefix + "command-"));
    assertNotEquals(prefix, SupportBundleCgroupSupport.unitPrefix(UUID.randomUUID()));
  }

  @Test
  public void testAppliedLimitsAreLoggedAtInfo() {
    cgroupSupport.logAppliedLimits(
        universe,
        node,
        "UniverseLogs",
        ok(
            "out\n\nWRAPPER_LIMITS cpu_mode=quota cpu_max=15000/100000 memory_high=1 memory_max=2"
                + " unit=yb-sb-x slice=ybsupportbundle-throttled.slice reason=none\n"));

    assertTrue(
        logs.list.stream()
            .anyMatch(
                e ->
                    e.getLevel() == Level.INFO
                        && e.getFormattedMessage().contains("cpu_mode=quota cpu_max=15000/100000")
                        && e.getFormattedMessage().contains("unit=yb-sb-x")
                        && e.getFormattedMessage()
                            .contains("slice=ybsupportbundle-throttled.slice")));
    assertEquals(0, warnings("CPU cap"));
  }

  @Test
  public void testNoCpuCapIsWarnedOncePerUniverseAndReason() {
    when(universe.getUniverseUUID()).thenReturn(UUID.randomUUID());

    cgroupSupport.logAppliedLimits(universe, node, "a", ok(trailer("none", "not_cgroup_v2")));
    cgroupSupport.logAppliedLimits(universe, node, "b", ok(trailer("none", "not_cgroup_v2")));
    cgroupSupport.logAppliedLimits(universe, node, "c", ok(trailer("none", "no_user_manager")));
    // Asked for: SystemLogs runs as another user and only gets the priority settings.
    cgroupSupport.logAppliedLimits(universe, node, "d", ok(trailer("none", "no_cgroup")));

    assertEquals(1, warnings("without a CPU cap, first seen on node 'u-n1': not_cgroup_v2"));
    assertEquals(1, warnings("without a CPU cap, first seen on node 'u-n1': no_user_manager"));
    assertEquals(0, warnings("no_cgroup"));
  }

  @Test
  public void testMissingTrailerIsWarned() {
    cgroupSupport.logAppliedLimits(universe, node, "a", ok("out\n"));
    assertEquals(1, warnings("reported no limits"));
  }

  @Test
  public void testStripTrailerKeepsTheCommandsOutputAndCode() {
    ShellResponse response = ok(trailer("freeze", "none"));
    response.code = 3;
    response.durationMs = 42;

    ShellResponse stripped = SupportBundleCgroupSupport.stripTrailer(response);

    assertEquals("out\n", stripped.getMessage());
    assertEquals(3, stripped.getCode());
    assertEquals(42, stripped.durationMs);
    ShellResponse plain = ok("no trailer here\n");
    assertSame(plain, SupportBundleCgroupSupport.stripTrailer(plain));
  }

  @Test
  public void testStopBundleUnitsStopsOnlyThisBundlesScopes() {
    ArgumentCaptor<List<String>> command = ArgumentCaptor.forClass(List.class);
    when(mockNodeUniverseManager.runCommand(
            eq(node), eq(universe), command.capture(), any(), eq(false)))
        .thenReturn(ok(""));

    cgroupSupport.stopBundleUnits(BUNDLE_UUID, universe, node);

    assertEquals("bash", command.getValue().get(0));
    assertEquals("-c", command.getValue().get(1));
    assertEquals(
        "export XDG_RUNTIME_DIR=/run/user/$(id -u);"
            + " systemctl --user stop 'yb-sb-33333333-*.scope' 2>/dev/null || true",
        command.getValue().get(2));
  }

  @Test
  public void testStopBundleUnitsNeverThrows() {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenThrow(new RuntimeException("node unreachable"));

    cgroupSupport.stopBundleUnits(BUNDLE_UUID, universe, node);

    assertEquals(1, warnings("node unreachable"));
  }

  @Test
  public void testMergeKeepsTheCommandsTimeoutLoggingAndRedactionButNotItsUser() {
    ShellProcessContext base = ShellProcessContext.builder().timeoutSecs(5L).build();
    ShellProcessContext command =
        ShellProcessContext.builder()
            .sshUser("someone-else")
            .timeoutSecs(30L)
            .logCmdOutput(true)
            .redactedVals(Map.of("secret", "REDACTED"))
            .build();

    ShellProcessContext merged = cgroupSupport.mergeCgroupContext(base, command);

    assertEquals(30L, merged.getTimeoutSecs());
    assertTrue(merged.isLogCmdOutput());
    assertEquals(Map.of("secret", "REDACTED"), merged.getRedactedVals());
    assertEquals(base.getSshUser(), merged.getSshUser());
  }
}
