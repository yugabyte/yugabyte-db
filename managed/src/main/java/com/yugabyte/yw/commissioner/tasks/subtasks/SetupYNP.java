// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class SetupYNP extends AbstractTaskBase {
  public static final int DEFAULT_NODE_AGENT_PORT = 9070;

  private final NodeUniverseManager nodeUniverseManager;
  private final NodeAgentManager nodeAgentManager;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected SetupYNP(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      NodeAgentManager nodeAgentManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeAgentManager = nodeAgentManager;
  }

  public static class Params extends NodeTaskParams {
    public String nodeAgentInstallDir;
    public String sshUser;
    public UUID customerUuid;
    public boolean sudoAccess;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  void removeNodeAgentDirectory(
      NodeDetails node, Universe universe, ShellProcessContext shellContext, String nodeAgentHome) {
    StringBuilder sb = new StringBuilder();
    sb.append("rm -rf ");
    sb.append(nodeAgentHome);
    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.info("Clearing node-agent directory: {}", command);
    nodeUniverseManager.runCommand(node, universe, command, shellContext).isSuccess();
  }

  private Path getNodeAgentPackagePath(Universe universe, NodeDetails node) {
    String output =
        nodeUniverseManager
            .runCommand(node, universe, Arrays.asList("uname", "-sm"), shellContext)
            .processErrors()
            .extractRunCommandOutput();
    if (StringUtils.isBlank(output)) {
      throw new RuntimeException("Unknown OS and Arch output: " + output);
    }
    // Output is like Linux x86_64.
    String[] parts = output.split("\\s+", 2);
    if (parts.length != 2) {
      throw new RuntimeException("Unknown OS and Arch output: " + output);
    }
    return nodeAgentManager.getNodeAgentPackagePath(
        OSType.parse(parts[0].trim()), ArchType.parse(parts[1].trim()));
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    if (taskParams().sshUser != null) {
      shellContext = shellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }

    String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);
    Path ynpStagingDir = Paths.get(customTmpDirectory, "ynp");
    Path targetPackagePath = ynpStagingDir.resolve(Paths.get("release", "node-agent.tgz"));
    Path nodeAgentHomePath = Paths.get(taskParams().nodeAgentInstallDir, NodeAgent.NODE_AGENT_DIR);
    Path packagePath = getNodeAgentPackagePath(universe, node);

    // Clean up the previous stale data.
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (optional.isPresent()) {
      NodeAgent nodeAgent = optional.get();
      if (nodeAgent != null) {
        nodeAgentManager.purge(nodeAgent);
      }
    }
    removeNodeAgentDirectory(node, universe, shellContext, nodeAgentHomePath.toString());

    // Clean and create the staging path where the node agent release will be uploaded.
    StringBuilder sb = new StringBuilder();
    sb.append("rm -rf ").append(ynpStagingDir);
    sb.append(" && mkdir -m 777 -p ").append(ynpStagingDir);

    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.info("Creating staging directory: {}", command);
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();

    // Create the release path and upload file as the current SSH user.
    command =
        ImmutableList.<String>builder()
            .add("mkdir", "-p")
            .add(targetPackagePath.getParent().toString())
            .build();
    log.info("Creating release path in staging directory: {}", command);
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();
    log.info("Uploading {} to {}", packagePath, targetPackagePath);
    nodeUniverseManager.uploadFileToNode(
        node, universe, packagePath.toString(), targetPackagePath.toString(), "755", shellContext);

    Path nodeAgentInstallPath = nodeAgentHomePath.getParent();

    sb.setLength(0);
    // Remove existing node agent folder.
    sb.append("rm -rf ").append(nodeAgentHomePath);
    // Create the node agent home directory.
    sb.append(" && mkdir -m 755 -p ").append(nodeAgentInstallPath);
    // Extract only the installer file.
    sb.append(" && mkdir -p ").append(ynpStagingDir).append("/thirdparty");
    sb.append(" && tar --no-same-owner -zxf ").append(targetPackagePath);
    sb.append(" --strip-components=2 -C ")
        .append(ynpStagingDir)
        .append("/thirdparty/ ")
        .append("--wildcards '*/thirdparty/*'");

    sb.append(" && tar --no-same-owner -zxf ").append(targetPackagePath);
    sb.append(" --exclude='*/node-agent' --exclude='*/preflight_check.sh'");
    sb.append(" --strip-components=3 -C ").append(ynpStagingDir);

    // Move the node-agent source folder to the right location.
    sb.append(" && mv -f ").append(ynpStagingDir);
    sb.append(" ").append(nodeAgentHomePath);
    // Change the owner to the current user.
    sb.append(" && chown -R $(id -u):$(id -g) ").append(nodeAgentHomePath);
    sb.append(" && chmod 755 ").append(nodeAgentHomePath);
    command = getCommand("/bin/bash", "-c", sb.toString());
    try {
      nodeUniverseManager
          .runCommand(node, universe, command, shellContext)
          .processErrors("Extracting node-agent failed");
    } catch (RuntimeException e) {
      throw e;
    }
  }

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    if (taskParams().sudoAccess) {
      commandBuilder.add("sudo", "-H");
    }
    return commandBuilder.add(args).build();
  }
}
