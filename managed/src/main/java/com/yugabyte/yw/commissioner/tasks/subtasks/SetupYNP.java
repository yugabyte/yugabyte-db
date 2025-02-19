// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeAgentManager.InstallerFiles;
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
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import scala.*;

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
    public int nodeAgentPort = DEFAULT_NODE_AGENT_PORT;
    public String nodeAgentInstallDir;
    public String sshUser;
    public UUID customerUuid;
    public boolean sudoAccess;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  private NodeAgent createNodeAgent(Universe universe, NodeDetails node) {
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
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(node.cloudInfo.private_ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setPort(taskParams().nodeAgentPort);
    nodeAgent.setCustomerUuid(taskParams().customerUuid);
    nodeAgent.setOsType(OSType.parse(parts[0].trim()));
    nodeAgent.setArchType(ArchType.parse(parts[1].trim()));
    nodeAgent.setVersion(nodeAgentManager.getSoftwareVersion());
    nodeAgent.setHome(
        Paths.get(taskParams().nodeAgentInstallDir, NodeAgent.NODE_AGENT_DIR).toString());
    return nodeAgentManager.create(nodeAgent, false);
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (optional.isPresent()) {
      NodeAgent nodeAgent = optional.get();
      if (nodeAgent != null) {
        nodeAgentManager.purge(nodeAgent);
      }
    }

    if (taskParams().sshUser != null) {
      shellContext = shellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }
    String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);
    Path ynpStagingDir = Paths.get(customTmpDirectory, "ynp");
    NodeAgent nodeAgent = createNodeAgent(universe, node);
    InstallerFiles installerFiles = nodeAgentManager.getInstallerFiles(nodeAgent, ynpStagingDir);
    Set<String> dirs =
        installerFiles.getCreateDirs().stream()
            .map(dir -> dir.toString())
            .collect(Collectors.toSet());
    List<String> command = getCommand("mkdir", "-m", "777", "-p", ynpStagingDir.toString());
    log.info("Creating staging directory: {}", command);
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();

    // Create the child folders as the current SSH user so that the files can be uploaded.
    command = ImmutableList.<String>builder().add("mkdir", "-p").addAll(dirs).build();
    log.info("Creating directories {} for node agent {}", dirs, nodeAgent.getUuid());
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();
    installerFiles.getCopyFileInfos().stream()
        .forEach(
            f -> {
              log.info(
                  "Uploading {} to {} on node agent {}",
                  f.getSourcePath(),
                  f.getTargetPath(),
                  nodeAgent.getUuid());
              String filePerm = StringUtils.isBlank(f.getPermission()) ? "755" : f.getPermission();
              nodeUniverseManager.uploadFileToNode(
                  node,
                  universe,
                  f.getSourcePath().toString(),
                  f.getTargetPath().toString(),
                  filePerm,
                  shellContext);
            });

    Path nodeAgentHomePath = Paths.get(nodeAgent.getHome());
    Path nodeAgentInstallPath = nodeAgentHomePath.getParent();
    Path nodeAgentInstallerPath = nodeAgentHomePath.resolve("node-agent-provision.sh");

    StringBuilder sb = new StringBuilder();
    // Remove existing node agent folder.
    sb.append("rm -rf ").append(nodeAgentHomePath);
    // Create the node agent home directory.
    sb.append(" && mkdir -m 755 -p ").append(nodeAgentInstallPath);
    // Extract only the installer file.
    sb.append(" && mkdir -p ").append(ynpStagingDir).append("/thirdparty");
    sb.append(" && tar -zxf ").append(installerFiles.getPackagePath());
    sb.append(" --strip-components=2 -C ").append(ynpStagingDir).append("/thirdparty");

    sb.append(" && tar -zxf ").append(installerFiles.getPackagePath());
    sb.append(" --strip-components=3 -C ").append(ynpStagingDir);

    // Move the node-agent source folder to the right location.
    sb.append(" && mv -f ").append(ynpStagingDir);
    sb.append(" ").append(nodeAgentHomePath);
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
