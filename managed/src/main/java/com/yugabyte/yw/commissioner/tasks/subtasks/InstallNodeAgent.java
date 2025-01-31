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
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
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
  private final RuntimeConfGetter confGetter;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected InstallNodeAgent(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      NodeAgentManager nodeAgentManager,
      RuntimeConfGetter confGetter) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeAgentManager = nodeAgentManager;
    this.confGetter = confGetter;
  }

  public static class Params extends NodeTaskParams {
    public int nodeAgentPort = DEFAULT_NODE_AGENT_PORT;
    public String nodeAgentInstallDir;
    public String sshUser;
    public UUID customerUuid;
    public boolean reinstall;
    public boolean airgap;
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

  public NodeAgent install() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    NodeAgent nodeAgent = null;
    InstallerFiles installerFiles = null;
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (optional.isPresent()) {
      nodeAgent = optional.get();
      if (!taskParams().reinstall && nodeAgent.getState() == State.READY) {
        return nodeAgent;
      } else if (!confGetter.getGlobalConf(GlobalConfKeys.enableYNPProvisioning)) {
        nodeAgentManager.purge(nodeAgent);
      }
    }
    if (taskParams().sshUser != null) {
      shellContext = shellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }
    String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);
    Path stagingDir = Paths.get(customTmpDirectory, "node-agent-" + System.currentTimeMillis());
    Path nodeAgentSourcePath = stagingDir.resolve(NodeAgent.NODE_AGENT_DIR);
    List<String> command = getCommand("mkdir", "-m", "777", "-p", stagingDir.toString());
    log.info("Creating staging directory: {}", command);
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();

    if (!confGetter.getGlobalConf(GlobalConfKeys.enableYNPProvisioning)) {
      nodeAgent = createNodeAgent(universe, node);
      final NodeAgent nAgent = nodeAgent;
      installerFiles = nodeAgentManager.getInstallerFiles(nodeAgent, nodeAgentSourcePath);
      Set<String> dirs =
          installerFiles.getCreateDirs().stream()
              .map(dir -> dir.toString())
              .collect(Collectors.toSet());

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
                    nAgent.getUuid());
                String filePerm =
                    StringUtils.isBlank(f.getPermission()) ? "755" : f.getPermission();
                nodeUniverseManager.uploadFileToNode(
                    node,
                    universe,
                    f.getSourcePath().toString(),
                    f.getTargetPath().toString(),
                    filePerm,
                    shellContext);
              });
    } else {
      Path ynpStagingDir = Paths.get(customTmpDirectory, "ynp");
      installerFiles = nodeAgentManager.getInstallerFiles(nodeAgent, nodeAgentSourcePath);
      command = getCommand("mkdir", "-m", "777", "-p", nodeAgentSourcePath.toString());
      log.info("Creating staging directory: {}", command);
      nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();

      command = getCommand("mv", ynpStagingDir.toString() + "/*", nodeAgentSourcePath.toString());
      log.info("Moving node-agent files to relevant location: {}", command);
      nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();
    }

    Path nodeAgentHomePath = Paths.get(nodeAgent.getHome());
    Path nodeAgentInstallPath = nodeAgentHomePath.getParent();
    Path nodeAgentInstallerPath = nodeAgentHomePath.resolve("node-agent-installer.sh");
    String certDirectoryPath = nodeAgentSourcePath.toString() + "/cert/";

    command =
        getCommand(
            "find",
            certDirectoryPath,
            "-mindepth",
            "1",
            "-maxdepth",
            "1",
            "-type",
            "d",
            "-exec",
            "basename",
            "{}",
            "\\;");
    log.info("Retreiving cert Directory: {}", command);
    String certDirectory =
        nodeUniverseManager
            .runCommand(node, universe, command, shellContext)
            .processErrors()
            .extractRunCommandOutput();

    StringBuilder sb = new StringBuilder();
    // Remove existing node agent folder.
    sb.append("rm -rf ").append(nodeAgentHomePath);
    // Create the node agent home directory.
    sb.append(" && mkdir -m 755 -p ").append(nodeAgentInstallPath);
    // Extract only the installer file.
    sb.append(" && tar -zxf ").append(installerFiles.getPackagePath());
    sb.append(" --strip-components=3 -C ").append(nodeAgentSourcePath);
    sb.append(" --wildcards -- */node-agent-installer.sh");
    // Move the node-agent source folder to the right location.
    sb.append(" && mv -f ").append(nodeAgentSourcePath);
    sb.append(" ").append(nodeAgentHomePath);
    // Remove the staging directory.
    sb.append(" && rm -rf ").append(stagingDir);
    // Change the owner to the current user.
    sb.append(" && chown -R $(id -u):$(id -g) ").append(nodeAgentHomePath);
    // Give executable permission to the installer script.
    sb.append(" && chmod +x ").append(nodeAgentInstallerPath);
    // Run the installer script.
    sb.append(" && ").append(nodeAgentInstallerPath);
    sb.append(" -c install --skip_verify_cert --disable_egress");
    sb.append(" --install_path ").append(nodeAgentInstallPath);
    sb.append(" --id ").append(nodeAgent.getUuid());
    sb.append(" --customer_id ").append(nodeAgent.getCustomerUuid());
    sb.append(" --cert_dir ").append(certDirectory);
    sb.append(" --node_name ").append(node.getNodeName());
    sb.append(" --node_ip ").append(node.cloudInfo.private_ip);
    sb.append(" --node_port ").append(String.valueOf(taskParams().nodeAgentPort));
    if (taskParams().airgap) {
      sb.append(" --airgap");
    }
    command = getCommand("/bin/bash", "-c", sb.toString());
    log.debug("Running node agent installation command: {}", command);

    try {
      nodeUniverseManager
          .runCommand(node, universe, command, shellContext)
          .processErrors("Installation failed");
    } catch (RuntimeException e) {
      nodeAgent.updateLastError(new YBAError(Code.INSTALLATION_ERROR, e.getMessage()));
      throw e;
    }
    nodeAgent.saveState(State.REGISTERED);
    sb.setLength(0);
    sb.append("systemctl");
    if (!taskParams().sudoAccess) {
      sb.append(" --user");
    }
    sb.append(" is-active --quiet yb-node-agent");
    command = getCommand("/bin/bash", "-c", sb.toString());
    log.debug("Waiting for node agent service to be running");
    log.debug("Running systemd command: {}", command);
    try {
      nodeUniverseManager
          .runCommand(node, universe, command, shellContext)
          .processErrors("Service startup failed");
    } catch (RuntimeException e) {
      nodeAgent.updateLastError(new YBAError(Code.SERVICE_START_ERROR, e.getMessage()));
      throw e;
    }
    return nodeAgent;
  }

  @Override
  public void run() {
    install();
  }

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    if (taskParams().sudoAccess) {
      commandBuilder.add("sudo", "-H");
    }
    return commandBuilder.add(args).build();
  }
}
