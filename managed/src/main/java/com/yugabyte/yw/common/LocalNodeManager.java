/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ShellResponse.ERROR_CODE_GENERIC_ERROR;
import static com.yugabyte.yw.common.ShellResponse.ERROR_CODE_SUCCESS;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Joiner;
import com.google.common.base.Predicate;
import com.google.common.collect.Sets;
import com.google.common.hash.Hashing;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.commissioner.tasks.subtasks.TransferXClusterCerts;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFileAttributeView;
import java.nio.file.attribute.PosixFilePermission;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import java.util.stream.Stream;
import javax.inject.Singleton;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.io.input.ReversedLinesFileReader;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

/** Node manager that runs all the processes locally. Processess are bind to loopback interfaces. */
@Singleton
@Slf4j
public class LocalNodeManager {
  public static final String MASTER_EXECUTABLE = "yb-master";
  public static final String TSERVER_EXECUTABLE = "yb-tserver";
  public static final String CONTROLLER_EXECUTABLE = "yb-controller-server";

  private static final String MAX_MEM_RATIO_TSERVER = "0.1";
  private static final String MAX_MEM_RATIO_MASTER = "0.05";

  private static final int ERROR_LINES_TO_DUMP = 200;
  private static final int OUT_LINES_TO_DUMP = 200;
  private static final int EXIT_LINES_TO_DUMP = 10;

  private static final String LOOPBACK_PREFIX = "127.0.";
  public static final String COMMAND_OUTPUT_PREFIX = "Command output:";
  private static final boolean RUN_LOG_THREADS = true;

  private static Map<String, String> versionBinPathMap = new HashMap<>();

  private Map<Integer, String> predefinedConfig = null;
  private Set<String> usedIPs = Sets.newConcurrentHashSet();

  private Map<String, NodeInfo> nodesByNameMap = new ConcurrentHashMap<>();
  private Map<String, String> nodeFSByNameMap = new ConcurrentHashMap<>();

  private SpecificGFlags additionalGFlags;

  @Setter private int ipRangeStart = 2;
  @Setter private int ipRangeEnd = 100;

  @Inject private RuntimeConfGetter confGetter;

  public void setPredefinedConfig(Map<Integer, String> predefinedConfig) {
    this.predefinedConfig = predefinedConfig;
  }

  public void addVersionBinPath(String version, String binPath) {
    versionBinPathMap.put(version, binPath);
  }

  public void setAdditionalGFlags(SpecificGFlags additionalGFlags) {
    log.debug("Set additional gflags: {}", additionalGFlags.getPerProcessFlags().value);
    this.additionalGFlags = additionalGFlags;
  }

  // Temporary method.
  public void shutdown() {
    nodesByNameMap.forEach(
        (name, node) -> {
          Map<UniverseTaskBase.ServerType, Process> processMap = node.processMap;
          processMap.forEach(
              (serverType, process) -> {
                try {
                  if (serverType == UniverseTaskBase.ServerType.TSERVER) {
                    String baseDir = nodeFSByNameMap.get(name);
                    killPostMasterProcess(baseDir);
                  }
                  log.debug("Destroying {}", process.pid());
                  killProcess(process.pid());
                } catch (Exception e) {
                  log.error("Failed to destroy process " + process, e);
                }
              });
        });

    nodesByNameMap.clear();
    nodeFSByNameMap.clear();
  }

  private void killPostMasterProcess(String path) {
    // kill the postmaster pid's as well for the nodes.
    String filePath = path + "pg_data/postmaster.pid";
    List<String> postmasterPid = readProcessIdsFromFile(filePath);

    if (postmasterPid != null && !postmasterPid.isEmpty()) {
      for (String pid : postmasterPid) {
        if (pid.matches("^\\d+$")) {
          List<String> pgPids = new ArrayList<>();
          try {
            ProcessBuilder psProcessBuilder =
                new ProcessBuilder("ps", "--no-headers", "-p", pid, "--ppid", pid, "-o", "pid:1");
            Process psProcess = psProcessBuilder.start();

            BufferedReader psReader =
                new BufferedReader(new InputStreamReader(psProcess.getInputStream()));
            String line;
            while ((line = psReader.readLine()) != null) {
              pgPids.add(line.trim());
            }

            int psExitCode = psProcess.waitFor();

            if (psExitCode == 0 && !pgPids.isEmpty()) {
              for (String pgPid : pgPids) {
                if (pgPid.matches("^\\d+$")) {
                  log.info("Killing postgres PID: {} ...", pgPid);
                  ProcessBuilder killProcessBuilder = new ProcessBuilder("kill", "-KILL", pgPid);
                  Process killProcess = killProcessBuilder.start();
                  int killExitCode = killProcess.waitFor();
                  if (killExitCode != 0) {
                    log.error("Failed to kill process with PID: {}", pgPid);
                  }
                } else {
                  log.error("Found invalid Postgres PID: {}. Skipped.", pgPid);
                }
              }
            }
          } catch (IOException | InterruptedException e) {
            log.error("pg process deletion failed with: {}", e.getMessage());
          }
        } else {
          log.error("Invalid process ID: {}", pid);
        }
      }
    } else {
      log.info("No valid process IDs found in the file.");
    }
  }

  private void killProcess(long pid) throws IOException, InterruptedException {
    try {
      terminateProcessAndSubprocesses(pid);
    } catch (SecurityException | IllegalArgumentException e) {
      System.err.println("Error occurred while terminating process: " + e.getMessage());
      e.printStackTrace();
    }
  }

  private void terminateProcessAndSubprocesses(long pid) {
    ProcessHandle.of(pid)
        .ifPresentOrElse(
            process -> {
              // Terminate the process and all its subprocesses
              Stream<ProcessHandle> descendants = process.descendants();
              descendants.forEach(ProcessHandle::destroy);
              process.destroy();
            },
            () -> {
              throw new IllegalArgumentException("No such process with PID: " + pid);
            });
  }

  // This does not clear the process map.
  public void killProcess(String nodeName, UniverseTaskBase.ServerType serverType)
      throws IOException, InterruptedException {
    NodeInfo nodeInfo = nodesByNameMap.get(nodeName);
    if (nodeInfo != null) {
      Process process = nodeInfo.processMap.get(serverType);
      if (process != null) {
        log.debug("Destroying process with pid {} for {}", process.pid(), nodeInfo.ip);
        killProcess(process.pid());
      }
    }
  }

  public void startProcess(
      UUID universeUuid, String nodeName, UniverseTaskBase.ServerType serverType) {
    Universe universe = Universe.getOrBadRequest(universeUuid);
    NodeInfo nodeInfo = nodesByNameMap.get(nodeName);
    UserIntent userIntent = universe.getCluster(nodeInfo.placementUUID).userIntent;
    startProcessForNode(userIntent, serverType, nodeInfo);
  }

  public boolean isProcessRunning(String nodeName, UniverseTaskBase.ServerType serverType) {
    NodeInfo nodeInfo = nodesByNameMap.get(nodeName);
    if (nodeInfo == null) {
      return false;
    }
    return nodeInfo.processMap.containsKey(serverType);
  }

  public void checkAllProcessesAlive() {
    nodesByNameMap.values().stream()
        .flatMap(n -> n.processMap.values().stream())
        .forEach(
            process -> {
              try {
                Optional<ProcessHandle> processHandle = ProcessHandle.of(process.pid());
                if (processHandle.isEmpty()) {
                  log.error("Process " + process.pid() + " doesn't exist!!");
                }
              } catch (Exception e) {
                log.error("Failed check process " + process, e);
              }
            });
    log.debug("Processes: ");
    ProcessHandle.allProcesses()
        .filter(ph -> ph.info().command().isPresent() && ph.info().command().get().contains("yb-"))
        .forEach(ph -> log.debug("PID: " + ph.pid() + " command: " + ph.info().command()));
  }

  private enum NodeState {
    CREATED,
    PROVISIONED
  }

  public class NodeInfo {
    private final String name;
    private final String region;
    private final UUID azUIID;
    private final String instanceType;
    private final UUID placementUUID;
    private NodeState state;
    private final String ip;
    private Map<UniverseTaskBase.ServerType, Process> processMap = new HashMap<>();
    private Map<String, String> tags = new HashMap<>();

    private NodeInfo(NodeDetails nodeDetails, NodeTaskParams nodeTaskParams) {
      this.name = nodeDetails.nodeName;
      this.region = nodeTaskParams.getRegion().getCode();
      this.instanceType = nodeTaskParams.instanceType;
      this.ip = pickNewIP(nodeDetails);
      log.debug("Picked {} for {}", this.ip, this.name);
      this.azUIID = nodeTaskParams.azUuid;
      this.placementUUID = nodeDetails.placementUuid;
      setState(NodeState.CREATED);
    }

    public void setState(NodeState state) {
      this.state = state;
    }

    @Override
    public String toString() {
      return "NodeInfo{"
          + "name='"
          + name
          + '\''
          + ", region='"
          + region
          + '\''
          + ", azUIID="
          + azUIID
          + ", instanceType='"
          + instanceType
          + '\''
          + ", state="
          + state
          + ", ip='"
          + ip
          + '\''
          + ", processMap="
          + processMap
          + '}';
    }
  }

  /**
   * Returns per-az statistics for number of nodes in current placement.
   *
   * @param placementUUID
   * @return
   */
  public Map<UUID, Integer> getNodeCount(UUID placementUUID) {
    Map<UUID, Integer> result = new HashMap<>();
    nodesByNameMap.values().stream()
        .filter(n -> n.placementUUID.equals(placementUUID))
        .forEach(n -> result.merge(n.azUIID, 1, Integer::sum));
    return result;
  }

  public static String getRawCommandOutput(String str) {
    String result = str.replaceFirst(COMMAND_OUTPUT_PREFIX, "");
    return result.strip();
  }

  public static LocalCloudInfo getCloudInfo(NodeDetails nodeDetails, Universe universe) {
    return getCloudInfo(universe.getCluster(nodeDetails.placementUuid).userIntent);
  }

  public static LocalCloudInfo getCloudInfo(UniverseDefinitionTaskParams.UserIntent userIntent) {
    Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    return CloudInfoInterface.get(provider);
  }

  public static String getOutput(Process process) throws IOException {
    StringBuilder builder = new StringBuilder();
    String separator = System.getProperty("line.separator");
    String line = null;
    try (BufferedReader reader =
        new BufferedReader(new InputStreamReader(process.getInputStream()))) {
      while ((line = reader.readLine()) != null) {
        builder.append(line).append(separator);
      }
    }
    return builder.toString();
  }

  public ShellResponse nodeCommand(
      NodeManager.NodeCommandType type, NodeTaskParams nodeTaskParam, List<String> commandArgs) {
    Universe universe = Universe.getOrBadRequest(nodeTaskParam.getUniverseUUID());
    NodeDetails nodeDetails = universe.getNode(nodeTaskParam.nodeName);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid).userIntent;
    ShellResponse response = null;
    Config config = confGetter.getStaticConf();
    final NodeInfo nodeInfo = nodesByNameMap.get(nodeTaskParam.nodeName);
    if (nodeInfo != null) {
      response = successResponse(convertNodeInfoToJson(nodeInfo));
    }
    Map<String, String> args = convertCommandArgListToMap(commandArgs);
    log.debug("Arguments: " + args);
    switch (type) {
      case Create:
        NodeInfo newNodeInfo = new NodeInfo(nodeDetails, nodeTaskParam);
        nodesByNameMap.put(newNodeInfo.name, newNodeInfo);
        nodeFSByNameMap.put(newNodeInfo.name, getNodeFSRoot(userIntent, newNodeInfo));
        response = successResponse(convertNodeInfoToJson(newNodeInfo));
        break;
      case Provision:
        if (nodeInfo == null) {
          log.debug("No node found for " + nodeTaskParam.nodeName);
        } else {
          nodeInfo.setState(NodeState.PROVISIONED);
        }
        break;
      case List:
        if (nodeInfo == null) {
          response = ShellResponse.create(ERROR_CODE_SUCCESS, "");
        }
        break;
      case Delete_Root_Volumes:
        response = ShellResponse.create(ERROR_CODE_SUCCESS, "Deleted!");
        break;
      case Tags:
        InstanceActions.Params taskParam = (InstanceActions.Params) nodeTaskParam;
        Map<String, String> tags =
            taskParam.tags != null ? taskParam.tags : userIntent.instanceTags;
        if (MapUtils.isEmpty(tags) && taskParam.deleteTags.isEmpty()) {
          throw new RuntimeException("Invalid params: no tags to add or remove");
        }
        nodeInfo.tags.putAll(tags);
        if (!taskParam.deleteTags.isEmpty()) {
          nodeInfo.tags.keySet().removeAll(Arrays.asList(taskParam.deleteTags.split(",")));
        }
        break;
      case Configure:
        AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) nodeTaskParam;
        switch (params.type) {
          case Everything:
            if (args.containsKey("--ybc_flags")) {
              Map<String, String> ybcGFlags = getGFlagsFromArgs(args, "--ybc_flags");
              processAndWriteGFLags(
                  args, ybcGFlags, userIntent, UniverseTaskBase.ServerType.CONTROLLER, nodeInfo);
            }
            commandArgs.addAll(NodeManager.getInlineWaitForClockSyncCommandArgs(this.confGetter));
            break;
          case Software:
            updateSoftwareOnNode(userIntent, params.ybSoftwareVersion);
            break;
          case Certs:
          case ToggleTls:
          case GFlags:
            UniverseTaskBase.ServerType processType =
                UniverseTaskBase.ServerType.valueOf(
                    params.getProperty("processType").toUpperCase());
            Map<String, String> gflags = getGFlagsFromArgs(args, "--gflags");
            Map<String, String> defaultFlags = getGFlagsFromArgs(args, "--extra_gflags");
            defaultFlags.putAll(gflags);
            processAndWriteGFLags(args, defaultFlags, userIntent, processType, nodeInfo);
            break;
        }
        break;
      case Control:
        AnsibleClusterServerCtl.Params param = (AnsibleClusterServerCtl.Params) nodeTaskParam;
        UniverseTaskBase.ServerType process =
            UniverseTaskBase.ServerType.valueOf(param.process.toUpperCase());
        switch (param.command) {
          case "start":
            startProcessForNode(userIntent, process, nodeInfo);
            break;
          case "stop":
            stopProcessForNode(userIntent, process, nodeInfo, false);
            break;
        }
        break;
      case Destroy:
        for (UniverseTaskBase.ServerType serverType :
            nodeInfo.processMap.keySet().toArray(new UniverseTaskBase.ServerType[0])) {
          stopProcessForNode(userIntent, serverType, nodeInfo, true);
        }
        nodesByNameMap.remove(nodeInfo.name);
        response = ShellResponse.create(ERROR_CODE_SUCCESS, "Success!");
        break;
      case Transfer_XCluster_Certs:
        transferXClusterCerts(nodeTaskParam, nodeInfo, userIntent);
        break;
      default:
    }
    log.debug("Response is {} for {} ", response, type);
    if (response == null) {
      response = ShellResponse.create(ERROR_CODE_GENERIC_ERROR, "Unknown!");
    }
    return response;
  }

  public void dumpProcessOutput(
      Universe universe, String nodeName, UniverseTaskBase.ServerType serverType) {

    NodeDetails nodeDetails = universe.getNode(nodeName);
    if (nodeDetails == null) {
      log.warn("Node {} not found", nodeName);
      return;
    }
    UniverseDefinitionTaskParams.UserIntent intent =
        universe.getCluster(nodeDetails.placementUuid).userIntent;
    NodeInfo nodeInfo = nodesByNameMap.get(nodeName);
    Process process = nodeInfo.processMap.get(serverType);
    String logsDirPath = getLogsDir(intent, serverType, nodeInfo);
    File logsDir = new File(logsDirPath);
    try {
      // Access files inside master/logs directory
      File[] files = logsDir.listFiles();
      if (files != null) {
        for (File file : files) {
          log.debug("Catching o/p for file {}", file.getName());
          // Log or process master file
          if (file.exists()) {
            log.error(
                "Node {} process {} last {} error logs: \n {}",
                nodeName,
                serverType,
                ERROR_LINES_TO_DUMP,
                getLogOutput(
                    nodeName + "_" + serverType + "_ERR", file, (l) -> true, ERROR_LINES_TO_DUMP));
          }
        }
      }
    } catch (IOException ignored) {
    }
  }

  public String getTmpDir(
      Map<String, String> gflags,
      String nodeName,
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    if (gflags.containsKey(GFlagsUtil.TMP_DIRECTORY)) {
      String tmpDir = gflags.get(GFlagsUtil.TMP_DIRECTORY);
      if (tmpDir.contains("/tmp/local/")) {
        return tmpDir;
      }
      String nodeRoot = getNodeRoot(userIntent, nodeName);
      // Otherwise it will be too long string
      String hashedRoot = Hashing.md5().hashString(nodeRoot, Charset.defaultCharset()).toString();
      return "/tmp/local/" + hashedRoot;
    }
    return "/tmp";
  }

  private String getLogOutput(String prefix, File file, Predicate<String> filter, int maxLines)
      throws IOException {
    StringBuilder stringBuilder = new StringBuilder();
    String line;
    int counter = 0;
    try (ReversedLinesFileReader reader =
        new ReversedLinesFileReader(file, Charset.defaultCharset())) {
      while (counter < maxLines) {
        line = reader.readLine();
        if (line == null) {
          break;
        }
        if (filter.apply(line)) {
          counter++;
          stringBuilder.append("\n");
          stringBuilder.append(line);
        }
      }
    }
    return stringBuilder.toString();
  }

  private void transferXClusterCerts(
      NodeTaskParams taskParams,
      NodeInfo nodeInfo,
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    TransferXClusterCerts.Params tParams = (TransferXClusterCerts.Params) taskParams;
    String homeDir = getNodeRoot(userIntent, nodeInfo);
    String producerCertsDirOnTarget =
        replaceYbHome(tParams.producerCertsDirOnTarget.getAbsolutePath(), userIntent, nodeInfo);
    String replicationGroupName = tParams.replicationGroupName;
    String path = producerCertsDirOnTarget + "/" + replicationGroupName;
    switch (tParams.action.toString()) {
      case "copy":
        try {
          copyCerts(path, tParams.rootCertPath.getAbsolutePath());
        } catch (IOException e) {
          throw new RuntimeException(e);
        }
        break;
      case "remove":
        File producerCertsDir = new File(path);
        if (producerCertsDir.exists()) {
          FileUtils.deleteDirectory(producerCertsDir);
        }
        break;
      default:
    }
  }

  private Map<String, String> getGFlagsFromArgs(Map<String, String> args, String key) {
    if (!args.containsKey(key)) {
      return Collections.emptyMap();
    }
    return (Map<String, String>) Json.fromJson(Json.parse(args.get(key)), Map.class);
  }

  private String replaceYbHome(
      String path, UniverseDefinitionTaskParams.UserIntent userIntent, NodeInfo nodeInfo) {
    return path.replace(CommonUtils.DEFAULT_YB_HOME_DIR, getNodeRoot(userIntent, nodeInfo));
  }

  private void processAndWriteGFLags(
      Map<String, String> args,
      Map<String, String> gflags,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UniverseTaskBase.ServerType serverType,
      NodeInfo nodeInfo) {
    try {
      for (String key : new ArrayList<>(gflags.keySet())) {
        String value = gflags.get(key);
        value = replaceYbHome(value, userIntent, nodeInfo);
        gflags.put(key, value);
      }
      if (!gflags.containsKey(GFlagsUtil.DEFAULT_MEMORY_LIMIT_TO_RAM_RATIO)
          && serverType != UniverseTaskBase.ServerType.CONTROLLER) {
        gflags.put(
            GFlagsUtil.DEFAULT_MEMORY_LIMIT_TO_RAM_RATIO,
            serverType == UniverseTaskBase.ServerType.TSERVER
                ? MAX_MEM_RATIO_TSERVER
                : MAX_MEM_RATIO_MASTER);
      }
      if (gflags.containsKey(GFlagsUtil.TMP_DIRECTORY)) {
        String tmpDir = getTmpDir(gflags, nodeInfo.name, userIntent);
        new File(tmpDir).mkdirs();
        gflags.put(GFlagsUtil.TMP_DIRECTORY, tmpDir);
      }
      processCerts(args, nodeInfo, userIntent);
      writeGFlagsToFile(userIntent, gflags, serverType, nodeInfo);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  private void processCerts(
      Map<String, String> args,
      NodeInfo nodeInfo,
      UniverseDefinitionTaskParams.UserIntent userIntent)
      throws IOException {
    String homeDir = getNodeRoot(userIntent, nodeInfo);
    String certsDir = CertificateHelper.getCertsNodeDir(homeDir);
    if (args.containsKey("--root_cert_path")) {
      copyCerts(
          certsDir,
          args.get("--root_cert_path"),
          args.get("--server_cert_path"),
          args.get("--server_key_path"));
    }
    if (args.containsKey("--root_cert_path_client_to_server")) {
      String certsForClientDir = CertificateHelper.getCertsForClientDir(homeDir);
      copyCerts(
          certsForClientDir,
          args.get("--root_cert_path_client_to_server"),
          args.get("--server_cert_path_client_to_server"),
          args.get("--server_key_path_client_to_server"));
    }
    if (args.containsKey("--client_cert_path")) {
      // These certs will be used for testing the connectivity of `yugabyte` client with postgres.
      String certsForYSQLClientDir = homeDir + "/.yugabytedb";
      copyCerts(
          certsForYSQLClientDir,
          args.get("--client_cert_path"),
          args.get("--client_key_path"),
          args.get("--root_cert_path_client_to_server"));
    }
  }

  private void copyCerts(String baseDir, String... certs) throws IOException {
    File baseDirFile = new File(baseDir);
    baseDirFile.mkdirs();
    for (String fileName : certs) {
      File certFile = new File(fileName);
      String certName = certFile.getName();
      if (certName.equals("ca.root.crt")) {
        certName = "ca.crt";
      }
      File targetFile = new File(baseDirFile + "/" + certName);
      Files.copy(certFile.toPath(), targetFile.toPath(), StandardCopyOption.REPLACE_EXISTING);
      setFilePermissions(targetFile.getAbsolutePath());
    }
  }

  public void setFilePermissions(String filePath) throws IOException {
    PosixFileAttributeView attributeView =
        Files.getFileAttributeView(
            Paths.get(filePath), PosixFileAttributeView.class, LinkOption.NOFOLLOW_LINKS);
    attributeView.setPermissions(
        Set.of(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE));
  }

  private synchronized void updateSoftwareOnNode(
      UniverseDefinitionTaskParams.UserIntent userIntent, String version) {
    Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    String newYBBinDir = versionBinPathMap.get(version);
    LocalCloudInfo localCloudInfo = getCloudInfo(userIntent);
    if (StringUtils.isNotEmpty(newYBBinDir)) {
      localCloudInfo.setYugabyteBinDir(newYBBinDir);
    }
    ProviderDetails details = provider.getDetails();
    ProviderDetails.CloudInfo cloudInfo = details.getCloudInfo();
    cloudInfo.setLocal(localCloudInfo);
    details.setCloudInfo(cloudInfo);
    provider.setDetails(details);
    provider.save();
  }

  public void startProcessForNode(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UniverseTaskBase.ServerType serverType,
      NodeInfo nodeInfo) {
    List<String> args = new ArrayList<>();
    String executable;
    LocalCloudInfo localCloudInfo = getCloudInfo(userIntent);
    switch (serverType) {
      case MASTER:
        executable = localCloudInfo.getYugabyteBinDir() + "/" + MASTER_EXECUTABLE;
        break;
      case TSERVER:
        executable = localCloudInfo.getYugabyteBinDir() + "/" + TSERVER_EXECUTABLE;
        break;
      case CONTROLLER:
        executable = localCloudInfo.getYbcBinDir() + "/" + CONTROLLER_EXECUTABLE;
        break;
      default:
        throw new IllegalStateException("Not supported type " + serverType);
    }
    args.add(executable);
    args.add("--flagfile=" + getNodeGFlagsFile(userIntent, serverType, nodeInfo));
    if (serverType != UniverseTaskBase.ServerType.CONTROLLER) {
      args.add("--fs_wal_dirs=" + getNodeFSRoot(userIntent, nodeInfo));
      args.add("--fs_data_dirs=" + getNodeFSRoot(userIntent, nodeInfo));
      args.add("--local_ip_for_outbound_sockets=" + nodeInfo.ip);
    }
    args.add("--log_dir=" + getLogsDir(userIntent, serverType, nodeInfo));
    ProcessBuilder procBuilder =
        new ProcessBuilder(args.toArray(new String[0])).redirectErrorStream(true);
    log.info("Starting process: {}", Joiner.on(" ").join(args));
    try {
      Process proc = procBuilder.start();
      Thread.sleep(200);
      if (!proc.isAlive()) {
        throw new RuntimeException(
            "Process exited with code " + proc.exitValue() + " and output " + getOutput(proc));
      }
      log.info("Started with pid {}", proc.pid());
      nodeInfo.processMap.put(serverType, proc);
      if (RUN_LOG_THREADS) {
        runLogsThreads(serverType, proc, nodeInfo);
      }
    } catch (InterruptedException | IOException e) {
      throw new RuntimeException(e);
    }
  }

  private void stopProcessForNode(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UniverseTaskBase.ServerType serverType,
      NodeInfo nodeInfo,
      boolean isDestroy) {
    Process process = nodeInfo.processMap.remove(serverType);
    if (process == null) {
      throw new IllegalStateException("No process of type " + serverType + " for " + nodeInfo.name);
    }
    log.debug("Killing process {}", process.pid());
    try {
      killProcess(process.pid());
    } catch (IOException | InterruptedException e) {
      System.err.println("Error occurred while terminating process: " + e.getMessage());
      e.printStackTrace();
    }
  }

  private static List<String> readProcessIdsFromFile(String filePath) {
    List<String> processIds = new ArrayList<>();
    File file = new File(filePath);
    if (file.exists()) {
      try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
        String line;
        while ((line = reader.readLine()) != null) {
          processIds.add(line.trim());
        }
      } catch (IOException e) {
        e.printStackTrace();
      }
    } else {
      log.error("{}, does not exist.", filePath);
    }

    return processIds;
  }

  private void writeGFlagsToFile(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Map<String, String> gflags,
      UniverseTaskBase.ServerType serverType,
      NodeInfo nodeInfo)
      throws IOException {
    Map<String, String> gflagsToWrite = new LinkedHashMap<>(gflags);
    if (additionalGFlags != null && serverType != UniverseTaskBase.ServerType.CONTROLLER) {
      gflagsToWrite.putAll(additionalGFlags.getPerProcessFlags().value.get(serverType));
    }
    String fileName = getNodeGFlagsFile(userIntent, serverType, nodeInfo);
    log.debug("Write gflags {} for {} to file {}", gflagsToWrite, serverType, fileName);
    File flagFileTmpPath = new File(fileName);
    if (!flagFileTmpPath.exists()) {
      flagFileTmpPath.getParentFile().mkdirs();
      flagFileTmpPath.createNewFile();
    }
    try (FileOutputStream fis = new FileOutputStream(flagFileTmpPath);
        OutputStreamWriter writer = new OutputStreamWriter(fis);
        BufferedWriter buf = new BufferedWriter(writer)) {
      for (String key : gflagsToWrite.keySet()) {
        buf.write("--" + key + "=" + gflagsToWrite.get(key));
        buf.newLine();
      }
      buf.flush();
    }
  }

  private void runLogsThreads(
      UniverseTaskBase.ServerType serverType, Process proc, NodeInfo nodeInfo) {
    runLogThread(serverType, nodeInfo, false, proc);
    runLogThread(serverType, nodeInfo, true, proc);
  }

  private void runLogThread(
      UniverseTaskBase.ServerType serverType, NodeInfo nodeInfo, boolean error, Process process) {
    Thread thread =
        new Thread(
            () -> {
              String line;
              InputStream inputStream = error ? process.getErrorStream() : process.getInputStream();
              try (BufferedReader in = new BufferedReader(new InputStreamReader(inputStream))) {
                while (process.isAlive()) {
                  while ((line = in.readLine()) != null) {
                    if (error) {
                      log.error("{}_{}: {}", nodeInfo.name, serverType, line);
                    } else {
                      log.debug("{}_{}: {}", nodeInfo.name, serverType, line);
                    }
                  }
                  Thread.sleep(10);
                }
              } catch (Exception e) {
                log.error("Failed to read: {}", e.getMessage());
              }
            });
    thread.setDaemon(true);
    thread.start();
  }

  private String pickNewIP(NodeDetails nodeDetails) {
    if (predefinedConfig != null) {
      return predefinedConfig.get(nodeDetails.getNodeIdx());
    }
    List<Integer> ports =
        Arrays.asList(
            nodeDetails.masterHttpPort,
            nodeDetails.masterRpcPort,
            nodeDetails.tserverHttpPort,
            nodeDetails.tserverRpcPort,
            nodeDetails.ysqlServerHttpPort,
            nodeDetails.yqlServerRpcPort);
    List<Integer> ips =
        IntStream.range(ipRangeStart, ipRangeEnd).boxed().collect(Collectors.toList());
    Collections.shuffle(ips);
    for (Integer lastTwoBytes : ips) {
      String ip = LOOPBACK_PREFIX + ((lastTwoBytes >> 8) & 0xFF) + "." + (lastTwoBytes & 0xFF);
      if (usedIPs.contains(ip)) {
        continue;
      }
      try {
        final InetAddress bindIp = InetAddress.getByName(ip);
        boolean success = true;
        for (Integer port : ports) {
          if (!isPortFree(bindIp, port)) {
            success = false;
            break;
          }
        }
        if (!success) {
          continue;
        }
      } catch (IOException e) {
        continue;
      }
      if (usedIPs.add(ip)) {
        return ip;
      }
    }
    throw new IllegalStateException("Cannot pick new ip with ports " + ports);
  }

  private static boolean isPortFree(InetAddress bindInterface, int port) throws IOException {
    try (ServerSocket ignored = new ServerSocket(port, 50, bindInterface)) {
    } catch (IOException e) {
      return false;
    }
    return true;
  }

  private ShellResponse successResponse(JsonNode jsonNode) {
    return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, jsonNode.toString());
  }

  public String getNodeRoot(UniverseDefinitionTaskParams.UserIntent userIntent, NodeInfo nodeInfo) {
    String binDir = getCloudInfo(userIntent).getDataHomeDir();
    String suffix = nodeInfo.name.substring(nodeInfo.name.length() - 2);
    if (nodeInfo.name.contains("readonly")) {
      suffix = "rr-" + suffix;
    }
    return binDir + "/" + nodeInfo.ip + "-" + suffix;
  }

  public String getNodeRoot(UniverseDefinitionTaskParams.UserIntent userIntent, String nodeName) {
    NodeInfo nodeInfo = nodesByNameMap.get(nodeName);
    String binDir = getCloudInfo(userIntent).getDataHomeDir();
    return binDir + "/" + nodeInfo.ip + "-" + nodeInfo.name.substring(nodeInfo.name.length() - 2);
  }

  public NodeInfo getNodeInfo(NodeDetails nodeDetails) {
    return nodesByNameMap.get(nodeDetails.nodeName);
  }

  private String getNodeFSRoot(
      UniverseDefinitionTaskParams.UserIntent userIntent, NodeInfo nodeInfo) {
    String res = getNodeRoot(userIntent, nodeInfo) + "/data/";
    new File(res).mkdirs();
    return res;
  }

  private String getLogsDir(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UniverseTaskBase.ServerType serverType,
      NodeInfo nodeInfo) {
    String res =
        getNodeRoot(userIntent, nodeInfo) + "/" + serverType.name().toLowerCase() + "/logs/";
    new File(res).mkdirs();
    return res;
  }

  public String getNodeGFlagsFile(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UniverseTaskBase.ServerType serverType,
      NodeInfo nodeInfo) {
    return getNodeRoot(userIntent, nodeInfo)
        + "/"
        + serverType.name().toLowerCase()
        + "/conf/server.conf";
  }

  private JsonNode convertNodeInfoToJson(NodeInfo nodeInfo) {
    ObjectNode objectNode = Json.newObject();
    if (nodeInfo == null) {
      return objectNode;
    }
    objectNode.put("name", nodeInfo.name);
    objectNode.put("public_ip", nodeInfo.ip);
    objectNode.put("private_ip", nodeInfo.ip);
    objectNode.put("region", nodeInfo.region);
    objectNode.put("instance_type", nodeInfo.instanceType);
    return objectNode;
  }

  public static Map<String, String> convertCommandArgListToMap(List<String> args) {
    Map<String, String> result = new HashMap<>();
    for (int i = 0; i < args.size(); i++) {
      String key = args.get(i);
      if (key.startsWith("--")) {
        String value = "";
        if (i < args.size() - 1 && (args.get(i + 1) == null || !args.get(i + 1).startsWith("--"))) {
          value = args.get(++i);
        }
        result.put(key, value);
      }
    }
    return result;
  }
}
