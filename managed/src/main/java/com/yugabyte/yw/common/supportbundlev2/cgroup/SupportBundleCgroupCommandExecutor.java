// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.cgroup;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeUniverseContext;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.NodeUniverseManager.RemoteBashRunSpec;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleCancellation;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleNodeStaging;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2LimitsExec.CpuFallback;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.function.Supplier;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Runs support-bundle YSQL, YCQL and yb-admin batches under the limits script, or plainly when
 * cgroups are off or the universe is Kubernetes. Both paths run the same built command, so they
 * cannot drift. These components always run as yugabyte.
 */
@Slf4j
@Singleton
public class SupportBundleCgroupCommandExecutor {

  private static final String YSQL_COMPONENT = "YSQLComponent";
  private static final String YCQL_COMPONENT = "YCQLComponent";
  private static final String YB_ADMIN_COMPONENT = "YbAdminComponent";

  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleCgroupSupport cgroupSupport;
  private final SupportBundleNodeStaging staging;

  @Inject
  public SupportBundleCgroupCommandExecutor(
      NodeUniverseManager nodeUniverseManager,
      SupportBundleCgroupSupport cgroupSupport,
      SupportBundleNodeStaging staging) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.cgroupSupport = cgroupSupport;
    this.staging = staging;
  }

  public ShellResponse runYsqlQueries(
      UUID bundleUuid,
      NodeDetails node,
      Universe universe,
      String dbName,
      List<String> queries,
      long timeoutSec,
      boolean authEnabled,
      boolean cpEnabled,
      String componentLabel) {
    if (!cgroupSupport.shouldUseCgroup(universe, node)) {
      return SupportBundleCancellation.throwIfCancelled(
          nodeUniverseManager.runYsqlSupportBundleBatchCommands(
              node, universe, dbName, queries, timeoutSec, authEnabled, cpEnabled));
    }

    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYsqlSupportBundleBatchRunSpec(
            node, universe, dbName, queries, timeoutSec, authEnabled, cpEnabled);

    return executeWithCgroup(
        bundleUuid,
        universe,
        node,
        YSQL_COMPONENT,
        componentLabel,
        "db=" + dbName,
        spec,
        () ->
            nodeUniverseManager.runYsqlSupportBundleBatchCommands(
                node, universe, dbName, queries, timeoutSec, authEnabled, cpEnabled));
  }

  public ShellResponse runYcqlQueries(
      UUID bundleUuid,
      NodeDetails node,
      Universe universe,
      String keyspace,
      List<String> queries,
      long timeoutSecs,
      String componentLabel) {
    if (!cgroupSupport.shouldUseCgroup(universe, node)) {
      return SupportBundleCancellation.throwIfCancelled(
          nodeUniverseManager.runYcqlSupportBundleBatchCommands(
              node, universe, keyspace, queries, timeoutSecs));
    }

    RemoteBashRunSpec spec =
        nodeUniverseManager.buildYcqlSupportBundleBatchRunSpec(
            node, universe, keyspace, queries, timeoutSecs);

    String contextDescription =
        StringUtils.isNotBlank(keyspace) ? "keyspace=" + keyspace : "keyspace=unset";
    return executeWithCgroup(
        bundleUuid,
        universe,
        node,
        YCQL_COMPONENT,
        componentLabel,
        contextDescription,
        spec,
        () ->
            nodeUniverseManager.runYcqlSupportBundleBatchCommands(
                node, universe, keyspace, queries, timeoutSecs));
  }

  public ShellResponse runYbAdminCommands(
      UUID bundleUuid,
      NodeDetails node,
      Universe universe,
      List<String> commands,
      List<String> sharedArgs,
      long timeoutSecs,
      String componentLabel) {
    List<String> commandArgs = sharedArgs == null ? Collections.emptyList() : sharedArgs;

    if (!cgroupSupport.shouldUseCgroup(universe, node)) {
      RemoteBashRunSpec spec =
          SupportBundleRemoteCommandBuilder.buildYbAdminSupportBundleBatchRunSpec(
              node, nodeUniverseManager, universe, commands, commandArgs, timeoutSecs);
      return SupportBundleCancellation.throwIfCancelled(
          nodeUniverseManager.runCommand(
              NodeUniverseContext.of(universe, node, spec.context),
              spec.command,
              false /* useBash */));
    }

    RemoteBashRunSpec spec =
        SupportBundleRemoteCommandBuilder.buildYbAdminSupportBundleBatchRunSpec(
            node, nodeUniverseManager, universe, commands, commandArgs, timeoutSecs);

    return executeWithCgroup(
        bundleUuid,
        universe,
        node,
        YB_ADMIN_COMPONENT,
        componentLabel,
        "ybAdminCommands=" + commands.size(),
        spec,
        () -> {
          RemoteBashRunSpec directSpec =
              SupportBundleRemoteCommandBuilder.buildYbAdminSupportBundleBatchRunSpec(
                  node, nodeUniverseManager, universe, commands, commandArgs, timeoutSecs);
          return nodeUniverseManager.runCommand(
              NodeUniverseContext.of(universe, node, directSpec.context),
              directSpec.command,
              false /* useBash */);
        });
  }

  /**
   * Runs the batch under {@value SupportBundleV2LimitsExec#SCRIPT_NAME}, staged with the bundle's
   * other scripts. Falls back to {@code directFallback} when the script can't be staged; the client
   * isn't frozen, since its work runs in the tserver or master.
   */
  private ShellResponse executeWithCgroup(
      UUID bundleUuid,
      Universe universe,
      NodeDetails node,
      String componentType,
      String componentLabel,
      String contextDescription,
      RemoteBashRunSpec spec,
      Supplier<ShellResponse> directFallback) {
    // These components run as yugabyte by design, so no caller ssh user is carried over.
    ShellProcessContext baseContext =
        ShellProcessContext.builder().timeoutSecs(spec.context.getTimeoutSecs()).build();
    String limitsScript;
    try {
      limitsScript =
          staging.stage(bundleUuid, universe, node, cgroupSupport.localScriptPath(), baseContext);
    } catch (Exception e) {
      SupportBundleCancellation.rethrowIfCancelled(e);
      log.warn(
          "{} on universe='{}' node='{}' component='{}' {}: could not stage {}; running without"
              + " limits: {}",
          componentType,
          universe.getName(),
          node.getNodeName(),
          componentLabel,
          contextDescription,
          SupportBundleV2LimitsExec.SCRIPT_NAME,
          e.getMessage());
      return SupportBundleCancellation.throwIfCancelled(directFallback.get());
    }
    List<String> argv =
        SupportBundleV2LimitsExec.argv(
            limitsScript,
            cgroupSupport.limitsFor(
                bundleUuid,
                componentLabel,
                spec.context.getTimeoutSecs(),
                CpuFallback.NONE,
                false /* noCgroup */),
            spec.command);
    ShellResponse response =
        nodeUniverseManager.runCommand(
            NodeUniverseContext.of(
                universe, node, cgroupSupport.mergeCgroupContext(baseContext, spec.context)),
            argv,
            false /* useBash */);
    response = SupportBundleCancellation.throwIfCancelled(response);
    cgroupSupport.logAppliedLimits(universe, node, componentLabel, response);
    // YSQL and YCQL write the response into the bundle, so the trailer comes out first.
    return SupportBundleCgroupSupport.stripTrailer(response);
  }
}
