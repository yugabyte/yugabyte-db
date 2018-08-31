// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import play.Application;
import play.api.Play;
import play.libs.Json;
import org.yaml.snakeyaml.Yaml;

import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class KubernetesCommandExecutor extends AbstractTaskBase {
  public enum CommandType {
    HELM_INIT,
    HELM_INSTALL,
    HELM_DELETE,
    VOLUME_DELETE,
    POD_INFO;

    public String getSubTaskGroupName() {
      switch (this) {
        case HELM_INIT:
          return UserTaskDetails.SubTaskGroupType.HelmInit.name();
        case HELM_INSTALL:
          return UserTaskDetails.SubTaskGroupType.HelmInstall.name();
        case HELM_DELETE:
          return UserTaskDetails.SubTaskGroupType.HelmDelete.name();
        case VOLUME_DELETE:
          return UserTaskDetails.SubTaskGroupType.KubernetesVolumeDelete.name();
        case POD_INFO:
          return UserTaskDetails.SubTaskGroupType.KubernetesPodInfo.name();
      }
      return null;
    }
  }

  @Inject
  KubernetesManager kubernetesManager;

  @Inject
  Application application;

  static final Pattern nodeNamePattern = Pattern.compile(".*-n(\\d+)+");

  @Override
  public void initialize(ITaskParams params) {
    this.kubernetesManager = Play.current().injector().instanceOf(KubernetesManager.class);
    this.application = Play.current().injector().instanceOf(Application.class);
    super.initialize(params);
  }

  public static class Params extends AbstractTaskParams {
    public UUID providerUUID;
    public CommandType commandType;
    public UUID universeUUID;
    // We use the nodePrefix as Helm Chart's release name,
    // so we would need that for any sort helm operations.
    public String nodePrefix;
  }

  protected KubernetesCommandExecutor.Params taskParams() {
    return (KubernetesCommandExecutor.Params)taskParams;
  }

  @Override
  public void run() {
    // TODO: add checks for the shell process handler return values.
    switch (taskParams().commandType) {
      case HELM_INIT:
        kubernetesManager.helmInit(taskParams().providerUUID);
        break;
      case HELM_INSTALL:
        String overridesFile = this.generateHelmOverride();
        kubernetesManager.helmInstall(taskParams().providerUUID, taskParams().nodePrefix, overridesFile);
        break;
      case HELM_DELETE:
        kubernetesManager.helmDelete(taskParams().providerUUID, taskParams().nodePrefix);
        break;
      case VOLUME_DELETE:
        kubernetesManager.deleteStorage(taskParams().providerUUID, taskParams().nodePrefix);
        break;
      case POD_INFO:
        processNodeInfo();
        break;
    }
  }

  private void processNodeInfo() {
    ShellProcessHandler.ShellResponse podResponse = kubernetesManager.getPodInfos(taskParams().providerUUID, taskParams().nodePrefix);
    JsonNode podInfos = parseShellResponseAsJson(podResponse);
    ObjectNode pods = Json.newObject();
    // TODO: add more validations around the pod info call, handle error conditions
    for (JsonNode podInfo: podInfos.path("items")) {
      ObjectNode pod = Json.newObject();
      JsonNode statusNode =  podInfo.path("status");
      JsonNode podSpec = podInfo.path("spec");
      pod.put("startTime", statusNode.path("startTime").asText());
      pod.put("status", statusNode.path("phase").asText());
      // TODO: change the podIP to use cname ENG-3490, we need related jira ENG-3491 as well done.
      pod.put("privateIP", statusNode.get("podIP").asText());
      pods.set(podSpec.path("hostname").asText(), pod);
    }

    Universe.UniverseUpdater updater = universe -> {
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      Set<NodeDetails> nodeDetailsSet = new HashSet<>();
      universeDetails.nodeDetailsSet.forEach(((NodeDetails nodeDetails) -> {
        if (nodeDetails.isMaster) {
          String masterPodName = nodeNameToPodName(nodeDetails.nodeName, true);
          NodeDetails masterPod = nodeDetails.clone();
          JsonNode pod = pods.get(masterPodName);
          masterPod.nodeName = masterPodName;
          masterPod.state = NodeDetails.NodeState.Live;
          masterPod.cloudInfo.private_ip = pod.get("privateIP").asText();
          masterPod.isTserver = false;
          nodeDetailsSet.add(masterPod);
        }
        NodeDetails tserverPod = nodeDetails.clone();
        String tserverPodName = nodeNameToPodName(nodeDetails.nodeName, false);
        JsonNode pod = pods.get(tserverPodName);
        tserverPod.nodeName = tserverPodName;
        tserverPod.cloudInfo.private_ip = pod.get("privateIP").asText();
        tserverPod.state = NodeDetails.NodeState.Live;
        tserverPod.isMaster = false;
        nodeDetailsSet.add(tserverPod);
      }));
      universeDetails.nodeDetailsSet = nodeDetailsSet;
      universe.setUniverseDetails(universeDetails);
    };
    Universe.saveDetails(taskParams().universeUUID, updater);
  }

  private String nodeNameToPodName(String nodeName, boolean isMaster) {
    Matcher matcher = nodeNamePattern.matcher(nodeName);
    if (!matcher.matches()) {
      throw new RuntimeException("Invalid nodeName : " + nodeName);
    }
    int nodeIdx = Integer.parseInt(matcher.group(1));
    return String.format("%s-%d", isMaster ? "yb-master": "yb-tserver", nodeIdx - 1);
  }

  private String generateHelmOverride() {
    Map<String, Object> overrides = new HashMap<String, Object>();
    Yaml yaml = new Yaml();
    // TODO: decide if the user want to expose all the services or just master
    overrides =(HashMap<String, Object>) yaml.load(
        application.resourceAsStream("k8s-expose-all.yml")
    );

    Universe u = Universe.get(taskParams().universeUUID);
    // TODO: This only takes into account primary cluster for Kuberentes, we need to
    // address ReadReplica clusters as well.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent;
    InstanceType instanceType = InstanceType.get(userIntent.providerType, userIntent.instanceType);
    if (instanceType == null) {
      LOG.error("Unable to fetch InstanceType for {}, {}",
          userIntent.providerType, userIntent.instanceType);
      throw new RuntimeException("Unable to fetch InstanceType " + userIntent.providerType +
          ": " +  userIntent.instanceType);
    }

    // Override resource limit based on instance type
    Map<String, Object> tserverResource = new HashMap<>();
    tserverResource.put("cpu", instanceType.numCores);
    tserverResource.put("memory", String.format("%.2fGi", instanceType.memSizeGB));
    overrides.put("resource", ImmutableMap.of(
        "tserver", ImmutableMap.of("requests", tserverResource)
    ));

    // Override image tag based on ybsoftwareversion
    overrides.put("Image", ImmutableMap.of("tag", userIntent.ybSoftwareVersion));

    // Override num of tserver replicas based on num nodes.
    overrides.put("replicas", ImmutableMap.of("tserver", userIntent.numNodes));

    try {
      Path tempFile = Files.createTempFile(taskParams().universeUUID.toString(), ".yml");
      BufferedWriter bw = new BufferedWriter(new FileWriter(tempFile.toFile()));
      yaml.dump(overrides, bw);
      return tempFile.toAbsolutePath().toString();
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Error writing Helm Override file!");
    }
  }
}
