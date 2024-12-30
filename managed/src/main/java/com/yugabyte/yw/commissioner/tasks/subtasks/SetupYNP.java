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
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  private void installPython(Universe universe, NodeDetails node, ShellProcessContext context) {
    String packageManager = null;
    List<String> command = null;
    try {
      command = getCommand("rpm", "--version");
      nodeUniverseManager.runCommand(node, universe, command, context).processErrors();
      packageManager = "rpm";
    } catch (Exception e) {
      try {
        command = getCommand("dpkg", "--version");
        nodeUniverseManager.runCommand(node, universe, command, context).processErrors();
        packageManager = "deb";
      } catch (Exception ex) {
        log.debug("Unsupported package manager. Cannot determine package status.");
      }
    }

    if (packageManager == null) {
      return;
    }

    if (packageManager.equals("rpm")) {
      if (node.cloudInfo.cloud.equals("gcp")) {
        command =
            getCommand(
                "sudo",
                "rpm",
                "--import",
                "https://repo.almalinux.org/almalinux/RPM-GPG-KEY-AlmaLinux");
        nodeUniverseManager.runCommand(node, universe, command, context).processErrors();
      }
      command = getCommand("sudo", "dnf", "install", "-y", "python3.11");
    } else if (packageManager.equals("deb")) {
      command = getCommand("sudo", "apt-get", "update");
      nodeUniverseManager.runCommand(node, universe, command, context).processErrors();
      command = getCommand("sudo", "apt-get", "install", "-y", "python3.11");
    }

    if (command != null) {
      nodeUniverseManager.runCommand(node, universe, command, context).processErrors();
      command = getCommand("sudo", "ln", "-s", "/usr/bin/python3.11", "/usr/bin/python");
      nodeUniverseManager.runCommand(node, universe, command, context).processErrors();
    }
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
        return;
      }
    }

    if (taskParams().sshUser != null) {
      shellContext = shellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }
    String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);
    Path ynpStagingDir = Paths.get(customTmpDirectory, "ynp");
    installPython(universe, node, shellContext);
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
  }

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    return commandBuilder.add(args).build();
  }
}
