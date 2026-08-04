// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class CheckNodeDataDirDiskSpace extends NodeTaskBase {

  private static final long DEFAULT_REQUIRED_FREE_SPACE_BYTES = 3L * 1024 * 1024 * 1024; // 3 GB
  private static final int DF_TIMEOUT_SECS = 15;

  public static class Params extends NodeTaskParams {
    /** Required free space in bytes. Default: 3 GB. */
    public long requiredFreeSpaceBytes = DEFAULT_REQUIRED_FREE_SPACE_BYTES;
  }

  @Inject
  protected CheckNodeDataDirDiskSpace(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUuid=%s,nodeName=%s)",
        super.getName(), taskParams().getUniverseUUID(), taskParams().nodeName);
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNode(taskParams().nodeName);

    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType
        == CloudType.local) {
      log.info("Skipping disk space check for local provider");
      return;
    }

    if (node == null) {
      throw new IllegalArgumentException(
          String.format(
              "Node %s not found in universe %s", taskParams().nodeName, universe.getName()));
    }

    String dataDir = Util.getDataDirectoryPath(universe, node, config);
    long requiredBytes = taskParams().requiredFreeSpaceBytes;
    long availableBytes;

    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType
        == CloudType.kubernetes) {
      throw new RuntimeException("Kubernetes provider not supported");
    }
    availableBytes = getAvailableSpaceVM(universe, node, dataDir);

    if (availableBytes < requiredBytes) {
      String message =
          String.format(
              "Node %s has insufficient free disk space on data dir %s: required %d bytes,"
                  + " available %d bytes",
              node.nodeName, dataDir, requiredBytes, availableBytes);
      log.error(message);
      throw new RuntimeException(message);
    }

    log.debug(
        "Node {} has sufficient free disk space on {}: {} bytes (required {} bytes)",
        node.nodeName,
        dataDir,
        availableBytes,
        requiredBytes);
  }

  private long getAvailableSpaceVM(Universe universe, NodeDetails node, String dataDir) {
    // Use double quotes for awk script so single-quote stripping (e.g. by node-agent) doesn't break
    // it
    List<String> command =
        ImmutableList.of(
            "/bin/bash", "-c", "df -k " + dataDir + " 2>/dev/null | awk \"FNR==2 {print \\$4}\"");
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(DF_TIMEOUT_SECS).build();

    ShellResponse response =
        nodeUniverseManager
            .runCommand(node, universe, command, context)
            .processErrors("Failed to get disk space on node " + node.nodeName);

    String output;
    try {
      output = response.extractRunCommandOutput().trim();
    } catch (RuntimeException e) {
      // "Command output:" prefix is normally added by run_node_action.py or NodeActionRunner
      output = response.getMessage() != null ? response.getMessage().trim() : "";
    }
    return parseAvailableKbToBytes(node.nodeName, dataDir, output);
  }

  private long parseAvailableKbToBytes(String nodeName, String dataDir, String output) {
    // Command outputs only awk 'FNR==2 {print $4}' = single number (available KB)
    String line = StringUtils.strip(output);
    if (StringUtils.isBlank(line)) {
      throw new RuntimeException(
          "Empty or invalid df output for node " + nodeName + " data dir " + dataDir);
    }
    try {
      long availableKb = Long.parseLong(line);
      if (availableKb < 0) {
        throw new RuntimeException(
            "Invalid available space (negative) for node " + nodeName + ": " + output);
      }
      return availableKb * 1024L;
    } catch (NumberFormatException e) {
      throw new RuntimeException(
          "Failed to parse available disk space for node " + nodeName + " (output: " + output + ")",
          e);
    }
  }
}
