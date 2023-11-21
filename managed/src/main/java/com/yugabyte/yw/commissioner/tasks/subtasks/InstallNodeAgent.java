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
import com.yugabyte.yw.models.NodeAgent.State;
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

@Slf4j
public class InstallNodeAgent extends AbstractTaskBase {
  public static final int DEFAULT_NODE_AGENT_PORT = 9070;

  private final NodeUniverseManager nodeUniverseManager;
  private final NodeAgentManager nodeAgentManager;
  private final ShellProcessContext shellContext =
      ShellProcessContext.builder()
          .logCmdOutput(true)
          .defaultSshPort(true)
          .customUser(true)
          .build();

  @Inject
  protected InstallNodeAgent(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      NodeAgentManager nodeAgentManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeAgentManager = nodeAgentManager;
  }

  public static class Params extends NodeTaskParams {
    public int nodeAgentPort = DEFAULT_NODE_AGENT_PORT;
    public String nodeAgentHome;
    public UUID customerUuid;
    public boolean reinstall;
    public boolean airgap;
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
    nodeAgent.setHome(taskParams().nodeAgentHome);
    return nodeAgentManager.create(nodeAgent, false);
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (optional.isPresent()) {
      NodeAgent nodeAgent = optional.get();
      if (!taskParams().reinstall && nodeAgent.getState() == State.READY) {
        return;
      }
      nodeAgentManager.purge(nodeAgent);
    }
    String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);
    NodeAgent nodeAgent = createNodeAgent(universe, node);
    Path baseTargetDir = Paths.get(customTmpDirectory, "node-agent-" + System.currentTimeMillis());
    InstallerFiles installerFiles = nodeAgentManager.getInstallerFiles(nodeAgent, baseTargetDir);
    Set<String> dirs =
        installerFiles.getCreateDirs().stream()
            .map(dir -> dir.toString())
            .collect(Collectors.toSet());
    StringBuilder sb = new StringBuilder();
    sb.append("mkdir -p ").append(baseTargetDir);
    sb.append(" && chmod 777 ").append(baseTargetDir);
    String baseTargetDirCommand = sb.toString();
    // Create the base directory with sudo first, make it writable for all users.
    // This is done because some on-prem nodes may not have write permission to /tmp.
    List<String> command = ImmutableList.of("sudo", "/bin/bash", "-c", baseTargetDirCommand);
    log.info("Creating base target directory: {}", baseTargetDirCommand);
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
              nodeUniverseManager
                  .uploadFileToNode(
                      node,
                      universe,
                      f.getSourcePath().toString(),
                      f.getTargetPath().toString(),
                      filePerm,
                      shellContext)
                  .processErrors();
            });
    Path nodeAgentSourcePath = baseTargetDir.resolve("node-agent");
    Path nodeAgentInstallerPath = Paths.get(taskParams().nodeAgentHome, "node-agent-installer.sh");
    sb.setLength(0);
    // Remove existing node agent folder.
    sb.append("rm -rf ").append(taskParams().nodeAgentHome);
    // Extract only the installer file.
    sb.append(" && tar -zxf ").append(installerFiles.getPackagePath());
    sb.append(" --strip-components=3 -C ").append(nodeAgentSourcePath);
    sb.append(" --wildcards */node-agent-installer.sh");
    // Move the node-agent source folder to the right location.
    sb.append(" && mv -f ").append(nodeAgentSourcePath);
    sb.append(" ").append(taskParams().nodeAgentHome);
    // Give executable permission to the installer script.
    sb.append(" && chmod +x ").append(nodeAgentInstallerPath);
    // Run the installer script.
    sb.append(" && ").append(nodeAgentInstallerPath);
    sb.append(" -c install --skip_verify_cert --disable_egress");
    sb.append(" --id ").append(nodeAgent.getUuid());
    sb.append(" --customer_id ").append(nodeAgent.getCustomerUuid());
    sb.append(" --cert_dir ").append(installerFiles.getCertDir());
    sb.append(" --node_name ").append(node.getNodeName());
    sb.append(" --node_ip ").append(node.cloudInfo.private_ip);
    sb.append(" --node_port ").append(String.valueOf(taskParams().nodeAgentPort));
    if (taskParams().airgap) {
      sb.append(" --airgap");
    }
    // Give executable permission to node-agent path.
    sb.append(" && chmod 755 /root ").append(taskParams().nodeAgentHome);
    // Remove the unused installer script.
    sb.append(" && rm -rf ").append(nodeAgentInstallerPath);
    String installCommand = sb.toString();
    log.debug("Running node agent installation command: {}", installCommand);
    command = ImmutableList.of("sudo", "-H", "/bin/bash", "-c", installCommand);
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();
  }
}
