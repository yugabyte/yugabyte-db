// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeAgentManager.InstallerFiles;
import com.yugabyte.yw.common.ShellProcessContext;
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
public class InstallNodeAgent extends NodeTaskBase {
  public static final int DEFAULT_NODE_AGENT_PORT = 9070;

  private final NodeAgentManager nodeAgentManager;
  private final ShellProcessContext defaultShellContext =
      ShellProcessContext.builder().useSshConnectionOnly(true).logCmdOutput(true).build();

  @Inject
  protected InstallNodeAgent(
      BaseTaskDependencies baseTaskDependencies, NodeAgentManager nodeAgentManager) {
    super(baseTaskDependencies);
    this.nodeAgentManager = nodeAgentManager;
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

  private SetupYNP.Params getSetupYNPParams(InstallNodeAgent.Params taskParams) {
    SetupYNP.Params params = new SetupYNP.Params();
    params.sshUser = taskParams.sshUser;
    params.nodeName = taskParams.nodeName;
    params.customerUuid = taskParams.customerUuid;
    params.setUniverseUUID(taskParams.getUniverseUUID());
    params.nodeAgentInstallDir = taskParams.nodeAgentInstallDir;
    params.sudoAccess = taskParams.sudoAccess;
    return params;
  }

  private NodeAgent createNodeAgent(
      Universe universe, NodeDetails node, String nodeAgentHome, ShellProcessContext shellContext) {
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
    nodeAgent.setHome(nodeAgentHome);
    return nodeAgentManager.create(nodeAgent, false);
  }

  private boolean doesNodeAgentPackageExist(
      NodeDetails node, Universe universe, ShellProcessContext shellContext, String nodeAgentHome) {
    StringBuilder sb = new StringBuilder();
    sb.append("test -f ");
    sb.append(nodeAgentHome);
    sb.append("/release/node-agent.tgz");
    String cmd = sb.toString();
    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.info("Running command: {}", cmd);
    return nodeUniverseManager.runCommand(node, universe, command, shellContext).isSuccess();
  }

  private Path getTopLevelPath(Path homePath, Path path) {
    if (path.equals(homePath) || !path.startsWith(homePath)) {
      return path;
    }
    Path topPath = path;
    while (topPath != null && !homePath.equals(topPath.getParent())) {
      topPath = topPath.getParent();
    }
    return topPath == null ? path : topPath;
  }

  public NodeAgent install() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    ShellProcessContext shellContext =
        taskParams().sshUser == null
            ? defaultShellContext
            : defaultShellContext.toBuilder().sshUser(taskParams().sshUser).build();
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (optional.isPresent()) {
      NodeAgent nodeAgent = optional.get();
      if (nodeAgent.getState() == State.READY && !taskParams().reinstall) {
        // Already ready and it is not a reinstall.
        return nodeAgent;
      }
      nodeAgentManager.purge(nodeAgent);
    }
    Path nodeAgentHomePath = Paths.get(taskParams().nodeAgentInstallDir, NodeAgent.NODE_AGENT_DIR);
    // Force update the package on reinstall.
    if (taskParams().reinstall
        || !doesNodeAgentPackageExist(node, universe, shellContext, nodeAgentHomePath.toString())) {
      // Re-download the installer payload on the remote node.
      SetupYNP task = createTask(SetupYNP.class);
      task.initialize(getSetupYNPParams(taskParams()));
      task.run();
    }
    // Create the node agent record.
    NodeAgent nodeAgent =
        createNodeAgent(universe, node, nodeAgentHomePath.toString(), shellContext);
    // Get the node agent installer files.
    InstallerFiles installerFiles =
        nodeAgentManager.getInstallerFiles(nodeAgent, nodeAgentHomePath, false /* certsOnly */);

    StringBuilder sb = new StringBuilder();
    // Clean up the directories except the release path.
    Set<String> deleteDirs =
        installerFiles.getCreateDirs().stream()
            .filter(d -> !installerFiles.getPackagePath().startsWith(d))
            .map(d -> getTopLevelPath(nodeAgentHomePath, d).toString())
            .collect(Collectors.toSet());
    sb.append("rm -rf ").append(String.join(" ", deleteDirs));
    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.info("Deleting stale directories {} for node agent {}", deleteDirs, nodeAgent.getUuid());
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();

    // Create the new installation directories.
    Set<String> createDirs =
        installerFiles.getCreateDirs().stream()
            .filter(dir -> !installerFiles.getPackagePath().startsWith(dir))
            .map(dir -> dir.toString())
            .collect(Collectors.toSet());
    sb.setLength(0);
    // Create the child folders with full permission to allow upload.
    sb.append("umask 022 && mkdir -m 777 -p ").append(String.join(" ", createDirs));
    command = getCommand("/bin/bash", "-c", sb.toString());
    log.info("Creating directories {} for node agent {}", createDirs, nodeAgent.getUuid());
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();

    // Copy the files to the already created paths.
    installerFiles.getCopyFileInfos().stream()
        .filter(f -> !f.getTargetPath().equals(installerFiles.getPackagePath()))
        .forEach(
            f -> {
              String filePerm = StringUtils.isBlank(f.getPermission()) ? "755" : f.getPermission();
              log.info(
                  "Uploading {} to {} with perm %s on node agent {}",
                  f.getSourcePath(), f.getTargetPath(), filePerm, nodeAgent.getUuid());
              nodeUniverseManager.uploadFileToNode(
                  node,
                  universe,
                  f.getSourcePath().toString(),
                  f.getTargetPath().toString(),
                  filePerm,
                  shellContext);
            });

    Path nodeAgentInstallPath = nodeAgentHomePath.getParent();
    Path nodeAgentInstallerPath =
        nodeAgentHomePath.resolve(Paths.get("bin", NodeAgentManager.NODE_AGENT_INSTALLER_FILE));

    sb.setLength(0);
    // Restore directory permissions.
    sb.append("chmod 755 ").append(String.join(" ", createDirs));
    // Give executable permission to the installer script.
    sb.append(" && chmod +x ").append(nodeAgentInstallerPath);
    // Run the installer script.
    sb.append(" && ").append(nodeAgentInstallerPath);
    sb.append(" -c install --skip_verify_cert --disable_egress");
    sb.append(" --install_path ").append(nodeAgentInstallPath);
    sb.append(" --id ").append(nodeAgent.getUuid());
    sb.append(" --customer_id ").append(nodeAgent.getCustomerUuid());
    sb.append(" --cert_dir ").append(installerFiles.getCertDir());
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
    } catch (Exception e) {
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
