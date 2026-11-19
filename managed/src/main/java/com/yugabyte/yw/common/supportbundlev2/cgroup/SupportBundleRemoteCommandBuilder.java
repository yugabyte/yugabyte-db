// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.cgroup;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.NodeUniverseManager.RemoteBashRunSpec;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

/**
 * Builds remote bash run specs for support-bundle yb-admin commands.
 *
 * <p>Building the argv here rather than inside each executor is what lets the same command be run
 * plain or inside the cgroup wrapper without being written twice -- see {@link RemoteBashRunSpec}.
 * Every argument is passed through {@link SupportBundleCgroupSupport#shellQuote}, since master
 * addresses and cert paths come from universe state rather than from this code.
 */
public final class SupportBundleRemoteCommandBuilder {

  private SupportBundleRemoteCommandBuilder() {}

  static RemoteBashRunSpec buildYbAdminRunSpec(
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Universe universe,
      String ybAdminCommand,
      List<String> args,
      long timeoutSec) {
    List<String> argv =
        buildYbAdminArgv(node, nodeUniverseManager, universe, ybAdminCommand, args, timeoutSec);
    String innerBash =
        argv.stream()
            .map(SupportBundleRemoteCommandBuilder::shellQuote)
            .collect(Collectors.joining(" "));
    List<String> command = Arrays.asList("bash", "-c", innerBash);
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(timeoutSec).build();
    return new RemoteBashRunSpec(command, context);
  }

  /**
   * Batches several yb-admin calls into one remote shell. A support bundle runs a fixed list of
   * diagnostic commands per node, and issuing each over its own connection dominated the
   * component's runtime.
   */
  static RemoteBashRunSpec buildYbAdminSupportBundleBatchRunSpec(
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Universe universe,
      List<String> ybAdminCommands,
      List<String> sharedArgs,
      long timeoutSec) {
    String bashScript =
        buildYbAdminSupportBundleBatchBashScript(
            node, nodeUniverseManager, universe, ybAdminCommands, sharedArgs, timeoutSec);
    List<String> command = Arrays.asList("bash", "-c", bashScript);
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(timeoutSec).build();
    return new RemoteBashRunSpec(command, context);
  }

  /**
   * Emits a transcript: a {@code -- command:} header per call, stderr merged in, and {@code (exit
   * code N)} on failure. Ends in {@code exit 0} on purpose: some diagnostics fail on a healthy
   * cluster, and a non-zero exit would discard the output of the ones that succeeded.
   */
  static String buildYbAdminSupportBundleBatchBashScript(
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Universe universe,
      List<String> ybAdminCommands,
      List<String> sharedArgs,
      long timeoutSec) {
    List<String> args = sharedArgs == null ? List.of() : sharedArgs;
    StringBuilder script = new StringBuilder();
    for (String ybAdminCommand : ybAdminCommands) {
      List<String> argv =
          buildYbAdminArgv(node, nodeUniverseManager, universe, ybAdminCommand, args, timeoutSec);
      String quotedCommand =
          argv.stream()
              .map(SupportBundleRemoteCommandBuilder::shellQuote)
              .collect(Collectors.joining(" "));
      script
          .append("printf '%s\\n' ")
          .append(shellQuote("-- command: " + ybAdminCommand))
          .append("; ")
          .append(quotedCommand)
          .append(" 2>&1; _ec=$?; if [ $_ec -ne 0 ]; then printf '%s\\n' \"(exit code $_ec)\"; fi;")
          .append(" printf '\\n'; ");
    }
    script.append("exit 0");
    return script.toString();
  }

  private static List<String> buildYbAdminArgv(
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Universe universe,
      String ybAdminCommand,
      List<String> args,
      long timeoutSec) {
    List<String> command = new ArrayList<>();
    command.add(nodeUniverseManager.getYbHomeDir(node, universe) + "/master/bin/yb-admin");
    command.add("--master_addresses");
    command.add(universe.getMasterAddresses());
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (userIntent.enableNodeToNodeEncrypt) {
      command.add("-certs_dir_name");
      command.add(getCertsDir(nodeUniverseManager, node, universe));
    }
    command.add("-timeout_ms");
    command.add(String.valueOf(TimeUnit.SECONDS.toMillis(timeoutSec)));
    command.add(ybAdminCommand);
    if (args != null) {
      command.addAll(args);
    }
    return command;
  }

  /** Kubernetes mounts certs at a fixed path rather than under the yb home. */
  public static String getCertsDir(
      NodeUniverseManager nodeUniverseManager, NodeDetails node, Universe universe) {
    if (universe.getNodeDeploymentMode(node).equals(Common.CloudType.kubernetes)) {
      return NodeUniverseManager.K8S_CERTS_DIR;
    }
    return nodeUniverseManager.getYbHomeDir(node, universe) + NodeUniverseManager.CERTS_DIR;
  }

  /** Single-quotes a value for a remote shell, reopening the quote around embedded quotes. */
  static String shellQuote(String value) {
    if (value == null) {
      return "''";
    }
    return "'" + value.replace("'", "'\"'\"'") + "'";
  }
}
