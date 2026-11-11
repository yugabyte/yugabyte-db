// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.cgroup;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.Applied;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.CpuFallback;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.Limits;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ThreadLocalRandom;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Decides whether a support-bundle command runs under {@value
 * SupportBundleV2LimitsExec#SCRIPT_NAME} and with which limits, and logs what the node put in
 * force. The script applies them, best effort: a bundle collected without limits beats no bundle.
 * The limits are shared by every bundle's commands on a node, through {@value
 * SupportBundleV2LimitsExec#SLICE}.
 */
@Slf4j
@Singleton
class SupportBundleCgroupSupport {

  /**
   * Added to a command's timeout for its unit's RuntimeMaxSec, so YBA's own timeout fires first.
   */
  static final long UNIT_RUNTIME_MARGIN_SECS = 30L;

  static final String UNIT_PREFIX = "yb-sb-";
  private static final long STOP_UNITS_TIMEOUT_SECS = 60L;

  private final NodeUniverseManager nodeUniverseManager;
  private final RuntimeConfGetter confGetter;
  private final Map<UUID, String> lastLoggedFallbackReason = new ConcurrentHashMap<>();
  private final Set<String> noCpuCapWarned = ConcurrentHashMap.newKeySet();

  @Inject
  SupportBundleCgroupSupport(
      NodeUniverseManager nodeUniverseManager, RuntimeConfGetter confGetter) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.confGetter = confGetter;
  }

  /** Every VM node when the global flag is on, provisioned or not; never Kubernetes. */
  boolean shouldUseCgroup(Universe universe, NodeDetails node) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupEnabled)) {
      return false;
    }
    // The node's own cluster, not userIntent.provider: no database lookup, and correct on
    // multicloud universes.
    CloudType cloudType = universe.getNodeDeploymentMode(node);
    if (cloudType == null) {
      logOncePerUniverse(universe, "the node's cloud type is unknown");
      return false;
    }
    if (cloudType.equals(CloudType.kubernetes)) {
      logOncePerUniverse(universe, "not supported on Kubernetes universes");
      return false;
    }
    lastLoggedFallbackReason.remove(universe.getUniverseUUID());
    return true;
  }

  /** Asked for every command on every node, so each reason is logged once per universe. */
  private void logOncePerUniverse(Universe universe, String reason) {
    if (reason.equals(lastLoggedFallbackReason.put(universe.getUniverseUUID(), reason))) {
      return;
    }
    log.warn(
        "Support bundle on universe '{}' running without cgroup limits: {}",
        universe.getName(),
        reason);
  }

  /**
   * The configured percentages, clamped. A non-positive CPU percentage becomes 15 rather than no
   * limit, and a negative memory percentage its default (3 soft, 5 hard); zero memory percentages
   * stay zero, which means no memory cap. A soft limit above the hard one is lowered to it.
   */
  CgroupLimits getLimits() {
    int cpuPercent = confGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupCpuPercent);
    int memorySoftPercent =
        confGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupMemorySoftPercent);
    int memoryHardPercent =
        confGetter.getGlobalConf(GlobalConfKeys.supportBundleCgroupMemoryHardPercent);
    if (cpuPercent <= 0) {
      cpuPercent = 15;
    }
    if (cpuPercent > 100) {
      cpuPercent = 100;
    }
    if (memorySoftPercent < 0) {
      log.warn("memory_soft_percent ({}) is negative; using the default 3", memorySoftPercent);
      memorySoftPercent = 3;
    }
    if (memoryHardPercent < 0) {
      log.warn("memory_hard_percent ({}) is negative; using the default 5", memoryHardPercent);
      memoryHardPercent = 5;
    }
    if (memorySoftPercent > 100) {
      memorySoftPercent = 100;
    }
    if (memoryHardPercent > 100) {
      memoryHardPercent = 100;
    }
    if (memorySoftPercent > 0 && memoryHardPercent > 0 && memorySoftPercent > memoryHardPercent) {
      log.warn(
          "memory_soft_percent ({}) > memory_hard_percent ({}); clamping soft to hard",
          memorySoftPercent,
          memoryHardPercent);
      memorySoftPercent = memoryHardPercent;
    }
    return new CgroupLimits(cpuPercent, memorySoftPercent, memoryHardPercent);
  }

  /**
   * The limits for one command: the configured percentages, a unit named for the bundle and
   * component, RuntimeMaxSec from the command's timeout, and the node-wide slice.
   */
  Limits limitsFor(
      UUID bundleUuid,
      String componentLabel,
      long timeoutSecs,
      CpuFallback cpuFallback,
      boolean noCgroup) {
    CgroupLimits limits = getLimits();
    return new Limits(
        limits.cpuPercent,
        limits.memorySoftPercent,
        limits.memoryHardPercent,
        runtimeMaxSecs(timeoutSecs),
        unitName(bundleUuid, componentLabel),
        cpuFallback,
        noCgroup,
        SupportBundleV2LimitsExec.SLICE);
  }

  /** 0 for a command YBA itself doesn't time out. */
  static long runtimeMaxSecs(long timeoutSecs) {
    return timeoutSecs > 0 ? timeoutSecs + UNIT_RUNTIME_MARGIN_SECS : 0;
  }

  Path localScriptPath() {
    return Paths.get(
        AppConfigHelper.getDevopsHomePath(), "bin", SupportBundleV2LimitsExec.SCRIPT_NAME);
  }

  /** Every unit of one bundle shares this prefix, which is what cleanup stops. */
  static String unitPrefix(UUID bundleUuid) {
    return UNIT_PREFIX + bundleUuid.toString().substring(0, 8) + "-";
  }

  static String unitName(UUID bundleUuid, String componentLabel) {
    String component =
        StringUtils.isBlank(componentLabel)
            ? "command"
            : componentLabel.replaceAll("[^A-Za-z0-9_-]", "_");
    return unitPrefix(bundleUuid)
        + component
        + "-"
        + Integer.toHexString(ThreadLocalRandom.current().nextInt(0x100000, 0x1000000));
  }

  /**
   * Logs the limits the script reports in force. A command that ran without a CPU cap it asked for
   * is logged once per universe, since that is a property of the node type, not of the command.
   */
  void logAppliedLimits(
      Universe universe, NodeDetails node, String componentLabel, ShellResponse response) {
    Optional<Applied> applied =
        SupportBundleV2LimitsExec.parseTrailer(response == null ? null : response.getMessage());
    if (applied.isEmpty()) {
      log.warn(
          "Support bundle command on universe '{}' node '{}' component '{}' reported no limits",
          universe.getName(),
          node.getNodeName(),
          componentLabel);
      return;
    }
    Applied limits = applied.get();
    if ("none".equals(limits.cpuMode())
        && !"no_cgroup".equals(limits.reason())
        && noCpuCapWarned.add(universe.getUniverseUUID() + "/" + limits.reason())) {
      log.warn(
          "Support bundle on universe '{}' running without a CPU cap, first seen on node '{}': {}",
          universe.getName(),
          node.getNodeName(),
          limits.reason());
    }
    log.info(
        "Support bundle limits on universe '{}' node '{}' component '{}': cpu_mode={} cpu_max={}"
            + " memory_high={} memory_max={} unit={} slice={} reason={}",
        universe.getName(),
        node.getNodeName(),
        componentLabel,
        limits.cpuMode(),
        limits.cpuMax(),
        limits.memoryHigh(),
        limits.memoryMax(),
        limits.unit(),
        limits.slice(),
        limits.reason());
  }

  /** The script's trailer goes to stdout, and YSQL and YCQL write stdout into the bundle. */
  static ShellResponse stripTrailer(ShellResponse response) {
    if (response == null || response.getMessage() == null) {
      return response;
    }
    String stripped = SupportBundleV2LimitsExec.stripTrailer(response.getMessage());
    if (stripped.equals(response.getMessage())) {
      return response;
    }
    ShellResponse copy = ShellResponse.create(response.code, stripped);
    copy.durationMs = response.durationMs;
    copy.description = response.description;
    return copy;
  }

  /**
   * Stops this bundle's units still running on the node, e.g. after an abort. Best effort: a unit
   * left behind still ends at its RuntimeMaxSec.
   */
  void stopBundleUnits(UUID bundleUuid, Universe universe, NodeDetails node) {
    // The prefix is hex and fixed text, so it is safe inside the quoted pattern.
    String script =
        "export XDG_RUNTIME_DIR=/run/user/$(id -u);"
            + " systemctl --user stop '"
            + unitPrefix(bundleUuid)
            + "*.scope' 2>/dev/null || true";
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(
              node,
              universe,
              List.of("bash", "-c", script),
              ShellProcessContext.builder().timeoutSecs(STOP_UNITS_TIMEOUT_SECS).build(),
              false);
      if (!response.isSuccess()) {
        log.warn(
            "Could not stop support bundle units on node '{}': {}",
            node.getNodeName(),
            response.getMessage());
      }
    } catch (Exception e) {
      log.warn(
          "Could not stop support bundle units on node '{}': {}",
          node.getNodeName(),
          e.getMessage());
    }
  }

  /**
   * Copies only the caller's timeout, logging and redaction settings onto the wrapped command's
   * context; it runs as yugabyte.
   */
  ShellProcessContext mergeCgroupContext(
      ShellProcessContext base, ShellProcessContext commandContext) {
    return base.toBuilder()
        .timeoutSecs(commandContext.getTimeoutSecs())
        .logCmdOutput(commandContext.isLogCmdOutput())
        .redactedVals(commandContext.getRedactedVals())
        .build();
  }

  static final class CgroupLimits {
    final int cpuPercent;
    final int memorySoftPercent;
    final int memoryHardPercent;

    CgroupLimits(int cpuPercent, int memorySoftPercent, int memoryHardPercent) {
      this.cpuPercent = cpuPercent;
      this.memorySoftPercent = memorySoftPercent;
      this.memoryHardPercent = memoryHardPercent;
    }
  }
}
