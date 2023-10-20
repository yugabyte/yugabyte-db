/*
 * Copyright 2019 YugaByte, Inc. and Contributors
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
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFileAttributeView;
import java.nio.file.attribute.PosixFilePermission;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.MapUtils;
import play.libs.Json;

/** Node manager that runs all the processes locally. Processess are bind to loopback interfaces. */
@Singleton
@Slf4j
public class LocalNodeManager {
  public static final String MASTER_EXECUTABLE = "yb-master";
  public static final String TSERVER_EXECUTABLE = "yb-tserver";
  public static final String CONTROLLER_EXECUTABLE = "yb-controller-server";

  private static final String LOOPBACK_PREFIX = "127.0.0.";
  private static final String COMMAND_OUTPUT_PREFIX = "Command output:";
  private static final boolean RUN_LOG_THREADS = false;

  private Map<Integer, String> predefinedConfig = null;
  private Set<String> usedIPs = Sets.newConcurrentHashSet();

  private Map<String, NodeInfo> nodesByNameMap = new ConcurrentHashMap<>();

  @Inject private RuntimeConfGetter confGetter;

  public void setPredefinedConfig(Map<Integer, String> predefinedConfig) {
    this.predefinedConfig = predefinedConfig;
  }

  // Temporary method.
  public void shutdown() {
    nodesByNameMap.values().stream()
        .flatMap(n -> n.processMap.values().stream())
        .forEach(
            process -> {
              try {
                log.debug("Destroying {}", process.pid());
                process.destroy();
              } catch (Exception e) {
                log.error("Failed to destroy process " + process, e);
              }
            });
    nodesByNameMap.clear();
  }

  private enum NodeState {
    CREATED,
    PROVISIONED
  }

  private class NodeInfo {
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

  public ShellResponse runYsqlCommand(
      NodeDetails node, Universe universe, String dbName, String ysqlCommand, long timeoutSec) {
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
    LocalCloudInfo cloudInfo = getCloudInfo(node, universe);
    List<String> bashCommand = new ArrayList<>();
    bashCommand.add(cloudInfo.getYugabyteBinDir() + "/ysqlsh");
    bashCommand.add("-h");
    bashCommand.add(node.cloudInfo.private_ip);
    bashCommand.add("-t");
    bashCommand.add("-p");
    bashCommand.add(String.valueOf(node.ysqlServerRpcPort));
    bashCommand.add("-U");
    bashCommand.add("yugabyte");
    bashCommand.add("-d");
    bashCommand.add(dbName);
    bashCommand.add("-c");
    ysqlCommand = ysqlCommand.replace("\"", "");
    bashCommand.add(ysqlCommand);

    ProcessBuilder processBuilder =
        new ProcessBuilder(bashCommand.toArray(new String[0])).redirectErrorStream(true);
    if (cluster.userIntent.enableClientToNodeEncrypt && !cluster.userIntent.enableYSQLAuth) {
      processBuilder.environment().put("sslmode", "require");
    }
    try {
      log.debug("Running command {}", String.join(" ", bashCommand));
      Process process = processBuilder.start();
      long timeOut = timeoutSec * 1000;
      while (process.isAlive() && timeOut > 0) {
        Thread.sleep(50);
        timeOut -= 50;
      }
      if (process.isAlive()) {
        throw new RuntimeException("Timed out waiting for query");
      }
      return ShellResponse.create(process.exitValue(), COMMAND_OUTPUT_PREFIX + getOutput(process));
    } catch (IOException | InterruptedException e) {
      throw new RuntimeException(e);
    }
  }

  public static String getRawCommandOutput(String str) {
    String result = str.replaceFirst(COMMAND_OUTPUT_PREFIX, "");
    return result.strip();
  }

  private LocalCloudInfo getCloudInfo(NodeDetails nodeDetails, Universe universe) {
    return getCloudInfo(universe.getCluster(nodeDetails.placementUuid).userIntent);
  }

  private LocalCloudInfo getCloudInfo(UniverseDefinitionTaskParams.UserIntent userIntent) {
    Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    return CloudInfoInterface.get(provider);
  }

  private String getOutput(Process process) throws IOException {
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
            break;
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
            stopProcessForNode(process, nodeInfo);
            break;
        }
        break;
      case Destroy:
        nodesByNameMap.remove(nodeInfo.name);
        response = ShellResponse.create(ERROR_CODE_SUCCESS, "Success!");
        break;
      default:
    }
    log.debug("Response is {} for {} ", response, type);
    if (response == null) {
      response = ShellResponse.create(ERROR_CODE_GENERIC_ERROR, "Unknown!");
    }
    return response;
  }

  private Map<String, String> getGFlagsFromArgs(Map<String, String> args, String key) {
    return (Map<String, String>) Json.fromJson(Json.parse(args.get(key)), Map.class);
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
        value = value.replace(CommonUtils.DEFAULT_YB_HOME_DIR, getNodeRoot(userIntent, nodeInfo));
        gflags.put(key, value);
      }
      processCerts(args, gflags, nodeInfo, userIntent);
      writeGFlagsToFile(userIntent, gflags, serverType, nodeInfo);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  private void processCerts(
      Map<String, String> args,
      Map<String, String> gflags,
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
      PosixFileAttributeView attributeView =
          Files.getFileAttributeView(
              targetFile.toPath(), PosixFileAttributeView.class, LinkOption.NOFOLLOW_LINKS);
      attributeView.setPermissions(
          Set.of(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE));
    }
  }

  private void startProcessForNode(
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
      Thread.sleep(100);
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

  private void stopProcessForNode(UniverseTaskBase.ServerType serverType, NodeInfo nodeInfo) {
    Process process = nodeInfo.processMap.remove(serverType);
    if (process == null) {
      throw new IllegalStateException("No process of type " + serverType + " for " + nodeInfo.name);
    }
    process.destroy();
  }

  private void writeGFlagsToFile(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Map<String, String> gflags,
      UniverseTaskBase.ServerType serverType,
      NodeInfo nodeInfo)
      throws IOException {
    log.debug("Write gflags {} to file {}", gflags, serverType);
    File flagFileTmpPath = new File(getNodeGFlagsFile(userIntent, serverType, nodeInfo));
    if (!flagFileTmpPath.exists()) {
      flagFileTmpPath.getParentFile().mkdirs();
      flagFileTmpPath.createNewFile();
    }
    try (FileOutputStream fis = new FileOutputStream(flagFileTmpPath);
        OutputStreamWriter writer = new OutputStreamWriter(fis);
        BufferedWriter buf = new BufferedWriter(writer)) {
      for (String key : gflags.keySet()) {
        buf.write("--" + key + "=" + gflags.get(key));
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
    for (int suffix = 2; suffix < 255; suffix++) {
      String ip = LOOPBACK_PREFIX + suffix;
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

  private String getNodeRoot(
      UniverseDefinitionTaskParams.UserIntent userIntent, NodeInfo nodeInfo) {
    String binDir = getCloudInfo(userIntent).getDataHomeDir();
    return binDir + "/" + nodeInfo.ip + "-" + nodeInfo.name.substring(nodeInfo.name.length() - 2);
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

  private String getNodeGFlagsFile(
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
        if (i < args.size() - 1 && !args.get(i + 1).startsWith("--")) {
          value = args.get(++i);
        }
        result.put(key, value);
      }
    }
    return result;
  }
}
