// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.cgroup;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeUniverseContext;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleCancellation;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleNodeStaging;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.CpuFallback;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.Limits;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleNodeBinarySupport;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Runs a support-bundle node script from its staged copy, under {@value
 * SupportBundleV2LimitsExec#SCRIPT_NAME} when limits apply, and tears a bundle's staging down.
 */
@Singleton
@Slf4j
public class SupportBundleCgroupScriptExecutor {

  static final String RUN_TIMEOUT_FLAG = "--run_timeout_secs";
  static final String YB_HOME_DIR_FLAG = "--yb_home_dir";
  static final String TMP_DIR_FLAG = "--tmp_dir";

  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleCgroupSupport cgroupSupport;
  private final SupportBundleNodeBinarySupport binarySupport;
  private final SupportBundleNodeStaging staging;

  @Inject
  public SupportBundleCgroupScriptExecutor(
      NodeUniverseManager nodeUniverseManager,
      SupportBundleCgroupSupport cgroupSupport,
      SupportBundleNodeBinarySupport binarySupport,
      SupportBundleNodeStaging staging) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.cgroupSupport = cgroupSupport;
    this.binarySupport = binarySupport;
    this.staging = staging;
  }

  /**
   * Once per bundle, from the task's {@code finally}: stops the bundle's leftover units on nodes
   * that ran the limits script, then removes its staging from every node.
   */
  public void cleanupBundle(UUID bundleUuid, Universe universe) {
    if (bundleUuid == null || universe == null) {
      return;
    }
    staging.close(bundleUuid);
    List<NodeDetails> withLimits = new ArrayList<>();
    for (NodeDetails node : universe.getNodes()) {
      if (staging.isStaged(bundleUuid, node, SupportBundleV2LimitsExec.SCRIPT_NAME)) {
        withLimits.add(node);
      }
    }
    withLimits.forEach(node -> cgroupSupport.stopBundleUnits(bundleUuid, universe, node));
    staging.cleanup(bundleUuid, universe);
    binarySupport.forgetBundle(bundleUuid);
  }

  /** The staging id's directory on the node, where a yugabyte run's work and tar go. */
  public String stagingDir(
      UUID stagingId, Universe universe, NodeDetails node, ShellProcessContext stageContext) {
    return staging.dir(stagingId, universe, node, stageContext);
  }

  /** Removes a staging id's copies from every node, e.g. after an estimate. */
  public void cleanupStaging(UUID stagingId, Universe universe) {
    staging.cleanup(stagingId, universe);
  }

  /**
   * Runs one script entrypoint on a node from its staged copy. With {@code applyLimits}, it runs
   * under the limits script when the node allows. A command that runs as another user (SystemLogs)
   * gets only the script's priority settings, since the limits live in yugabyte's own systemd.
   *
   * @param stagingId the bundle, or a per-estimate id; its staging is removed by the caller
   * @param stageContext the context files are staged with, i.e. as yugabyte
   * @throws RuntimeException when the script can't be staged; without it there is nothing to run
   */
  public ShellResponse executeScript(
      UUID stagingId,
      Universe universe,
      NodeDetails node,
      String scriptPathRelative,
      Path localScriptPath,
      List<String> params,
      ShellProcessContext context,
      ShellProcessContext stageContext,
      String componentLabel,
      boolean applyLimits) {
    Objects.requireNonNull(stagingId, "stagingId");
    String remoteScript = staging.stage(stagingId, universe, node, localScriptPath, stageContext);
    String ybHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    // Another user (SystemLogs) can't write the yugabyte-owned directory, so keeps /tmp.
    String tmpDir =
        runsAsAnotherUser(context) ? null : staging.dir(stagingId, universe, node, stageContext);
    List<String> command = scriptCommand(remoteScript, ybHomeDir, tmpDir, params, 0L);

    List<String> argv =
        scriptCommand(
            remoteScript,
            ybHomeDir,
            tmpDir,
            params,
            SupportBundleCgroupSupport.runtimeMaxSecs(context.getTimeoutSecs()));
    boolean wrapped = false;
    if (applyLimits && cgroupSupport.shouldUseCgroup(universe, node)) {
      try {
        String limitsScript =
            staging.stage(stagingId, universe, node, cgroupSupport.localScriptPath(), stageContext);
        Limits limits =
            cgroupSupport.limitsFor(
                stagingId,
                componentLabel,
                context.getTimeoutSecs(),
                CpuFallback.FREEZE,
                runsAsAnotherUser(context));
        argv = SupportBundleV2LimitsExec.argv(limitsScript, limits, command);
        wrapped = true;
      } catch (Exception e) {
        SupportBundleCancellation.rethrowIfCancelled(e);
        log.warn(
            "Could not stage {} on node '{}'; running {} for component '{}' without limits: {}",
            SupportBundleV2LimitsExec.SCRIPT_NAME,
            node.getNodeName(),
            scriptPathRelative,
            componentLabel,
            e.getMessage());
      }
    }

    log.info(
        "Running {} for component '{}' on node '{}' as '{}' (setup as '{}')",
        scriptPathRelative,
        componentLabel,
        node.getNodeName(),
        StringUtils.defaultIfBlank(context.getSshUser(), "default"),
        StringUtils.defaultIfBlank(stageContext.getSshUser(), "default"));
    ShellResponse response =
        nodeUniverseManager.runCommand(
            NodeUniverseContext.of(universe, node, context), argv, false);
    response = stripOutputPrefix(SupportBundleCancellation.throwIfCancelled(response));
    if (!wrapped) {
      return response;
    }
    cgroupSupport.logAppliedLimits(universe, node, componentLabel, response);
    return SupportBundleCgroupSupport.stripTrailer(response);
  }

  // runCommand starts the output with "Command output:", over node agent on the first line itself;
  // the markers and the limits trailer are read line by line.
  private static ShellResponse stripOutputPrefix(ShellResponse response) {
    String message = response.getMessage();
    if (message == null || !message.startsWith(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX)) {
      return response;
    }
    ShellResponse copy =
        ShellResponse.create(
            response.code, message.substring(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX.length()));
    copy.durationMs = response.durationMs;
    copy.description = response.description;
    return copy;
  }

  /**
   * With {@code runTimeoutSecs}, support_bundle_utils.sh bounds itself on the node. A run under the
   * limits script passes 0, since the script bounds it. {@code ybHomeDir} is where
   * support_bundle_utils.sh finds the helpers YBA installs, under its bin directory. {@code
   * tmpDir}, when set, becomes the script's TMPDIR.
   */
  static List<String> scriptCommand(
      String remoteScript,
      String ybHomeDir,
      String tmpDir,
      List<String> params,
      long runTimeoutSecs) {
    List<String> command = new ArrayList<>(List.of("bash", remoteScript));
    if (runTimeoutSecs > 0) {
      command.add(RUN_TIMEOUT_FLAG);
      command.add(String.valueOf(runTimeoutSecs));
    }
    command.add(YB_HOME_DIR_FLAG);
    command.add(ybHomeDir);
    if (tmpDir != null) {
      command.add(TMP_DIR_FLAG);
      command.add(tmpDir);
    }
    if (params != null) {
      command.addAll(params);
    }
    return command;
  }

  public static boolean runsAsAnotherUser(ShellProcessContext context) {
    String sshUser = context.getSshUser();
    return StringUtils.isNotBlank(sshUser)
        && !ShellProcessContext.DEFAULT_REMOTE_USER.equals(sshUser);
  }
}
