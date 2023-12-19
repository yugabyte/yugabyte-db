/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateDetails;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.certmgmt.providers.CertificateProviderInterface;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.Service;
import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.bouncycastle.asn1.x509.GeneralName;
import org.yaml.snakeyaml.Yaml;
import play.libs.Json;

@Slf4j
public class KubernetesCommandExecutor extends UniverseTaskBase {

  private static final List<CommandType> skipNamespaceCommands =
      Arrays.asList(CommandType.POD_INFO, CommandType.COPY_PACKAGE, CommandType.YBC_ACTION);

  public enum CommandType {
    CREATE_NAMESPACE,
    APPLY_SECRET,
    HELM_INIT,
    HELM_INSTALL,
    HELM_UPGRADE,
    // TODO(bhavin192): should we just deprecate this? It is not used
    // anywhere in the code, and we use Helm operations to modify the
    // number of TServer nodes. The code which was using it has been
    // removed 3 years back in
    // 6c757362e4ba55921963e34c01e382f48843d959.
    UPDATE_NUM_NODES,
    HELM_DELETE,
    VOLUME_DELETE,
    VOLUME_DELETE_SHELL_MODE_MASTER,
    NAMESPACE_DELETE,
    POD_DELETE,
    DELETE_ALL_SERVER_TYPE_PODS,
    POD_INFO,
    STS_DELETE,
    PVC_EXPAND_SIZE,
    COPY_PACKAGE,
    YBC_ACTION,
    // The following flag is deprecated.
    INIT_YSQL;

    public String getSubTaskGroupName() {
      switch (this) {
        case CREATE_NAMESPACE:
          return UserTaskDetails.SubTaskGroupType.CreateNamespace.name();
        case APPLY_SECRET:
          return UserTaskDetails.SubTaskGroupType.ApplySecret.name();
        case HELM_INSTALL:
          return UserTaskDetails.SubTaskGroupType.HelmInstall.name();
        case HELM_UPGRADE:
          return UserTaskDetails.SubTaskGroupType.HelmUpgrade.name();
        case UPDATE_NUM_NODES:
          return UserTaskDetails.SubTaskGroupType.UpdateNumNodes.name();
        case HELM_DELETE:
          return UserTaskDetails.SubTaskGroupType.HelmDelete.name();
        case VOLUME_DELETE_SHELL_MODE_MASTER:
          return UserTaskDetails.SubTaskGroupType.KubernetesVolumeDeleteMasterShellMode.name();
        case VOLUME_DELETE:
          return UserTaskDetails.SubTaskGroupType.KubernetesVolumeDelete.name();
        case NAMESPACE_DELETE:
          return UserTaskDetails.SubTaskGroupType.KubernetesNamespaceDelete.name();
        case POD_DELETE:
          return UserTaskDetails.SubTaskGroupType.RebootingNode.name();
        case DELETE_ALL_SERVER_TYPE_PODS:
          return UserTaskDetails.SubTaskGroupType.DeleteAllServerTypePods.name();
        case POD_INFO:
          return UserTaskDetails.SubTaskGroupType.KubernetesPodInfo.name();
        case COPY_PACKAGE:
          return UserTaskDetails.SubTaskGroupType.KubernetesCopyPackage.name();
        case YBC_ACTION:
          return UserTaskDetails.SubTaskGroupType.KubernetesYbcAction.name();
        case INIT_YSQL:
          return UserTaskDetails.SubTaskGroupType.KubernetesInitYSQL.name();
        case STS_DELETE:
        case PVC_EXPAND_SIZE:
          return UserTaskDetails.SubTaskGroupType.ResizingDisk.name();
      }
      return null;
    }
  }

  public enum UpdateStrategy {
    RollingUpdate("RollingUpdate"),
    OnDelete("OnDelete");

    public final String value;

    UpdateStrategy(String value) {
      this.value = value;
    }

    public String toString() {
      return this.value;
    }
  }

  private final KubernetesManagerFactory kubernetesManagerFactory;
  private final ReleaseManager releaseManager;
  private final FileHelperService fileHelperService;
  private final YbcManager ybcManager;

  @Inject
  protected KubernetesCommandExecutor(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory,
      ReleaseManager releaseManager,
      FileHelperService fileHelperService,
      YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.kubernetesManagerFactory = kubernetesManagerFactory;
    this.releaseManager = releaseManager;
    this.fileHelperService = fileHelperService;
    this.ybcManager = ybcManager;
  }

  static final Pattern nodeNamePattern = Pattern.compile(".*-n(\\d+)+");

  // Added constant to compute CPU burst limit
  static final double burstVal = 1.2;

  public static class Params extends UniverseTaskParams {
    public UUID providerUUID;
    public String universeName;
    // universe details from user task params used to generate helm overrides
    public UniverseDefinitionTaskParams universeDetails;
    // Universe config is used for generating helm overrides.
    public Map<String, String> universeConfig;
    public CommandType commandType;
    public String helmReleaseName;
    public String namespace;
    public boolean isReadOnlyCluster;
    public String ybSoftwareVersion = null;
    public boolean enableNodeToNodeEncrypt = false;
    public boolean enableClientToNodeEncrypt = false;
    public UUID rootCA = null;
    public ServerType serverType = ServerType.EITHER;
    public int tserverPartition = 0;
    public int masterPartition = 0;
    public Map<String, Object> universeOverrides;
    public Map<String, Object> azOverrides;
    public String podName;
    public String newDiskSize;

    // Master addresses in multi-az case (to have control over different deployments).
    public String masterAddresses = null;

    // PlacementInfo to correctly set the placement details on the servers at start
    // as well as to control the replicas for each deployment.
    public PlacementInfo placementInfo = null;

    // RollingUpdate vs OnDelete upgrade strategy for K8s statefulset.
    public UpdateStrategy updateStrategy = KubernetesCommandExecutor.UpdateStrategy.RollingUpdate;

    // The target cluster's config.
    public Map<String, String> config = null;

    // YBC server name
    public String ybcServerName = null;
    public String command = null;
    public String azCode = null;
    public String pvcName = null;
    public int newPlacementAzMasterCount = 0;
  }

  protected KubernetesCommandExecutor.Params taskParams() {
    return (KubernetesCommandExecutor.Params) taskParams;
  }

  protected Map<String, String> getConfig() {
    // In case no config is provided, assume it is at the provider level
    // (for backwards compatibility).
    Map<String, String> config = taskParams().config;
    if (config == null) {
      Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);
      config = CloudInfoInterface.fetchEnvVars(provider);
    }
    return config;
  }

  @Override
  public void run() {
    String overridesFile;

    Map<String, String> config;
    if (taskParams().commandType.equals(CommandType.COPY_PACKAGE)
        || taskParams().commandType.equals(CommandType.YBC_ACTION)) {
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      PlacementInfo pi;
      if (taskParams().isReadOnlyCluster) {
        pi = universe.getUniverseDetails().getReadOnlyClusters().get(0).placementInfo;
      } else {
        pi = universe.getUniverseDetails().getPrimaryCluster().placementInfo;
      }
      Map<String, Map<String, String>> k8sConfigMap =
          KubernetesUtil.getKubernetesConfigPerPodName(
              pi, Collections.singleton(universe.getNode(taskParams().ybcServerName)));
      config = k8sConfigMap.get(taskParams().ybcServerName);
      if (config == null) {
        config = getConfig();
      }
    } else {
      config = getConfig();
    }

    if (!skipNamespaceCommands.contains(taskParams().commandType)
        && taskParams().namespace == null) {
      throw new IllegalArgumentException("namespace can be null only in case of POD_INFO");
    }

    // TODO: add checks for the shell process handler return values.
    switch (taskParams().commandType) {
      case CREATE_NAMESPACE:
        kubernetesManagerFactory.getManager().createNamespace(config, taskParams().namespace);
        break;
      case APPLY_SECRET:
        String pullSecret = this.getPullSecret();
        if (pullSecret != null) {
          kubernetesManagerFactory
              .getManager()
              .applySecret(config, taskParams().namespace, pullSecret);
        } else {
          log.debug("Pull secret is missing, skipping the pull secret creation.");
        }
        break;
      case HELM_INSTALL:
        overridesFile = this.generateHelmOverride();
        kubernetesManagerFactory
            .getManager()
            .helmInstall(
                taskParams().getUniverseUUID(),
                taskParams().ybSoftwareVersion,
                config,
                taskParams().providerUUID,
                taskParams().helmReleaseName,
                taskParams().namespace,
                overridesFile);
        break;
      case HELM_UPGRADE:
        overridesFile = this.generateHelmOverride();
        kubernetesManagerFactory
            .getManager()
            .helmUpgrade(
                taskParams().getUniverseUUID(),
                taskParams().ybSoftwareVersion,
                config,
                taskParams().helmReleaseName,
                taskParams().namespace,
                overridesFile);
        break;
      case UPDATE_NUM_NODES:
        int numNodes = this.getNumNodes();
        if (numNodes > 0) {
          boolean newNamingStyle =
              Universe.getOrBadRequest(taskParams().getUniverseUUID())
                  .getUniverseDetails()
                  .useNewHelmNamingStyle;
          kubernetesManagerFactory
              .getManager()
              .updateNumNodes(
                  config,
                  taskParams().helmReleaseName,
                  taskParams().namespace,
                  numNodes,
                  newNamingStyle);
        }
        break;
      case HELM_DELETE:
        kubernetesManagerFactory
            .getManager()
            .helmDelete(config, taskParams().helmReleaseName, taskParams().namespace);
        break;
      case VOLUME_DELETE_SHELL_MODE_MASTER:
        // We are deleting only master volumes for now,
        // perhaps tserver volumes should also be deleted ?
        Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
        String appLabelValue = "yb-master";
        kubernetesManagerFactory
            .getManager()
            .deleteUnusedPVCs(
                config,
                taskParams().namespace,
                taskParams().helmReleaseName,
                appLabelValue,
                universe.getUniverseDetails().useNewHelmNamingStyle,
                taskParams().newPlacementAzMasterCount); // ReplicaCount to Assert.
        break;
      case VOLUME_DELETE:
        kubernetesManagerFactory
            .getManager()
            .deleteStorage(config, taskParams().helmReleaseName, taskParams().namespace);
        break;
      case NAMESPACE_DELETE:
        kubernetesManagerFactory.getManager().deleteNamespace(config, taskParams().namespace);
        break;
      case POD_DELETE:
        kubernetesManagerFactory
            .getManager()
            .deletePod(config, taskParams().namespace, taskParams().podName);
        break;
      case DELETE_ALL_SERVER_TYPE_PODS:
        Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
        kubernetesManagerFactory
            .getManager()
            .deleteAllServerTypePods(
                config,
                taskParams().namespace,
                taskParams().serverType,
                taskParams().helmReleaseName,
                u.getUniverseDetails().useNewHelmNamingStyle);
        break;
      case POD_INFO:
        processNodeInfo();
        break;
      case STS_DELETE:
        u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
        boolean newNamingStyle = u.getUniverseDetails().useNewHelmNamingStyle;
        // Ideally we should have called KubernetesUtil.getHelmFullNameWithSuffix()
        String appName = (newNamingStyle ? taskParams().helmReleaseName + "-" : "") + "yb-tserver";
        kubernetesManagerFactory
            .getManager()
            .deleteStatefulSet(config, taskParams().namespace, appName);
        break;
      case PVC_EXPAND_SIZE:
        try {
          u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
          kubernetesManagerFactory
              .getManager()
              .expandPVC(
                  taskParams().getUniverseUUID(),
                  config,
                  taskParams().namespace,
                  taskParams().helmReleaseName,
                  "yb-tserver",
                  taskParams().newDiskSize,
                  u.getUniverseDetails().useNewHelmNamingStyle);
        } catch (Throwable e) {
          // Ignore exception from the actual expand task and handle it later in
          // KubernetesPostExpansionCheckVolume since we want to run the subsequent task
          // that re-creates the STS, and only then handle any errors.
          log.error("Orginal failure to expand volume: ", e);
        }
        break;
      case COPY_PACKAGE:
        u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
        NodeDetails nodeDetails = u.getNode(taskParams().ybcServerName);
        ybcManager.copyYbcPackagesOnK8s(
            config, u, nodeDetails, taskParams().getYbcSoftwareVersion());
        break;
      case YBC_ACTION:
        u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
        nodeDetails = u.getNode(taskParams().ybcServerName);
        List<String> commandArgs =
            Arrays.asList(
                "/bin/bash",
                "-c",
                String.format("/home/yugabyte/tools/k8s_ybc_parent.py %s", taskParams().command));
        ybcManager.performActionOnYbcK8sNode(config, nodeDetails, commandArgs);
        break;
    }
  }

  private Map<String, String> getClusterIpForLoadBalancer() {
    PlacementInfo pi = taskParams().placementInfo;

    Map<UUID, Map<String, String>> azToConfig = KubernetesUtil.getConfigPerAZ(pi);

    Map<String, String> serviceToIP = new HashMap<String, String>();

    for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      Map<String, String> config = entry.getValue();

      // TODO(bhavin192): we seem to be iterating over all the AZs
      // here, and still selecting services for only one AZ governed
      // by the taskParams().nodePrefix. Is it even required to
      // iterate in that case?
      List<Service> services =
          kubernetesManagerFactory
              .getManager()
              .getServices(config, taskParams().helmReleaseName, taskParams().namespace);

      services.forEach(
          service -> {
            serviceToIP.put(service.getMetadata().getName(), service.getSpec().getClusterIP());
          });
    }

    return serviceToIP;
  }

  private void processNodeInfo() {
    ObjectNode pods = Json.newObject();
    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UUID placementUuid =
        taskParams().isReadOnlyCluster
            ? u.getUniverseDetails().getReadOnlyClusters().get(0).uuid
            : u.getUniverseDetails().getPrimaryCluster().uuid;
    PlacementInfo pi = taskParams().placementInfo;
    String universename = taskParams().universeName;
    Map<UUID, Map<String, String>> azToConfig = KubernetesUtil.getConfigPerAZ(pi);
    Map<UUID, String> azToDomain = KubernetesUtil.getDomainPerAZ(pi);
    Provider provider = Provider.get(taskParams().providerUUID);
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    String nodePrefix = u.getUniverseDetails().nodePrefix;
    for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      UUID azUUID = entry.getKey();
      String azName = AvailabilityZone.get(azUUID).getCode();
      String regionName = AvailabilityZone.get(azUUID).getRegion().getCode();
      Map<String, String> config = entry.getValue();

      String helmReleaseName =
          KubernetesUtil.getHelmReleaseName(
              isMultiAz,
              nodePrefix,
              universename,
              azName,
              taskParams().isReadOnlyCluster,
              u.getUniverseDetails().useNewHelmNamingStyle);
      String namespace =
          KubernetesUtil.getKubernetesNamespace(
              isMultiAz,
              nodePrefix,
              azName,
              // TODO(bhavin192): it is not guaranteed that the config
              // we get here is an azConfig.
              config,
              u.getUniverseDetails().useNewHelmNamingStyle,
              taskParams().isReadOnlyCluster);

      List<Pod> podInfos =
          kubernetesManagerFactory.getManager().getPodInfos(config, helmReleaseName, namespace);

      for (Pod podInfo : podInfos) {
        ObjectNode pod = Json.newObject();
        pod.put("startTime", podInfo.getStatus().getStartTime());
        pod.put("status", podInfo.getStatus().getPhase());
        pod.put("az_uuid", azUUID.toString());
        pod.put("az_name", azName);
        pod.put("region_name", regionName);
        String hostname = podInfo.getSpec().getHostname();
        pod.put("hostname", hostname);

        int ybIdx = hostname.lastIndexOf("yb-");
        // The Helm full name is added to all the pods by the Helm
        // chart as a prefix, we are removing the yb-<server>-N part
        // from it. It is blank in case of old naming style.
        pod.put("helmFullNameWithSuffix", hostname.substring(0, ybIdx));
        // We leave out the Helm name prefix from the pod hostname,
        // and use the name like yb-<server>-N[_<az-name>] as nodeName
        // i.e. yb-master-0, and yb-master-0_az1 in case of multi-az.
        String nodeName = hostname.substring(ybIdx, hostname.length());
        nodeName = isMultiAz ? String.format("%s_%s", nodeName, azName) : nodeName;

        String podNamespace = podInfo.getMetadata().getNamespace();
        if (StringUtils.isBlank(podNamespace)) {
          throw new IllegalArgumentException(
              "metadata.namespace of pod " + hostname + " is empty. This shouldn't happen");
        }
        pod.put("namespace", podNamespace);

        pods.set(nodeName, pod);
      }
    }

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Set<NodeDetails> defaultNodes =
              universeDetails.getNodesInCluster(
                  taskParams().isReadOnlyCluster
                      ? universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid
                      : universe.getUniverseDetails().getPrimaryCluster().uuid);
          NodeDetails defaultNode = defaultNodes.iterator().next();
          Set<NodeDetails> nodeDetailsSet = new HashSet<>();
          Iterator<Map.Entry<String, JsonNode>> iter = pods.fields();
          Map<String, String> azConfig;
          Map<String, String> regionConfig;
          while (iter.hasNext()) {
            NodeDetails nodeDetail = defaultNode.clone();
            Map.Entry<String, JsonNode> pod = iter.next();
            String nodeName = pod.getKey();
            JsonNode podVals = pod.getValue();
            String hostname = podVals.get("hostname").asText();
            String namespace = podVals.get("namespace").asText();
            String helmFullNameWithSuffix = podVals.get("helmFullNameWithSuffix").asText();
            UUID azUUID = UUID.fromString(podVals.get("az_uuid").asText());
            String domain = azToDomain.get(azUUID);
            AvailabilityZone az = AvailabilityZone.getOrBadRequest(azUUID);
            Region region = az.getRegion();
            azConfig = CloudInfoInterface.fetchEnvVars(az);
            regionConfig = CloudInfoInterface.fetchEnvVars(region);
            String podAddressTemplate =
                getK8sPropertyFromConfigOrDefault(
                    null,
                    regionConfig,
                    azConfig,
                    "KUBE_POD_ADDRESS_TEMPLATE",
                    Util.K8S_POD_FQDN_TEMPLATE);
            if (nodeName.contains("master")) {
              nodeDetail.isTserver = false;
              nodeDetail.isMaster = true;
              nodeDetail.cloudInfo.private_ip =
                  KubernetesUtil.formatPodAddress(
                      podAddressTemplate,
                      hostname,
                      helmFullNameWithSuffix + "yb-masters",
                      namespace,
                      domain);
            } else {
              nodeDetail.isMaster = false;
              nodeDetail.isTserver = true;
              nodeDetail.cloudInfo.private_ip =
                  KubernetesUtil.formatPodAddress(
                      podAddressTemplate,
                      hostname,
                      helmFullNameWithSuffix + "yb-tservers",
                      namespace,
                      domain);
            }
            nodeDetail.cloudInfo.kubernetesNamespace = namespace;
            nodeDetail.cloudInfo.kubernetesPodName = hostname;
            if (isMultiAz) {
              nodeDetail.cloudInfo.az = podVals.get("az_name").asText();
              nodeDetail.cloudInfo.region = podVals.get("region_name").asText();
            }
            if (!confGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
              UserIntent userIntent =
                  taskParams().isReadOnlyCluster
                      ? u.getUniverseDetails().getReadOnlyClusters().get(0).userIntent
                      : u.getUniverseDetails().getPrimaryCluster().userIntent;
              nodeDetail.cloudInfo.instance_type = userIntent.getInstanceTypeForNode(nodeDetail);
            }
            nodeDetail.azUuid = azUUID;
            nodeDetail.placementUuid = placementUuid;
            nodeDetail.state = NodeDetails.NodeState.Live;
            // If read cluster is deployed in same AZ as primary, node names will be same. To make
            // them unique, append readonly tag.
            nodeDetail.nodeName =
                taskParams().isReadOnlyCluster
                    ? String.format("%s%s", nodeName, Universe.READONLY)
                    : nodeName;
            nodeDetailsSet.add(nodeDetail);
          }
          // Remove existing cluster nodes and add nodeDetailsSet
          // Don't remove all as we might delete other cluster nodes.
          universeDetails.nodeDetailsSet.removeAll(defaultNodes);
          universeDetails.nodeDetailsSet.addAll(nodeDetailsSet);
          universe.setUniverseDetails(universeDetails);
        };
    saveUniverseDetails(updater);
  }

  private String getPullSecret() {
    // Since the pull secret will always be the same across clusters,
    // it is always at the provider level.
    Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);
    if (provider != null) {
      Map<String, String> config = CloudInfoInterface.fetchEnvVars(provider);
      if (config.containsKey("KUBECONFIG_IMAGE_PULL_SECRET_NAME")) {
        return config.get("KUBECONFIG_PULL_SECRET");
      }
    }
    return null;
  }

  private int getNumNodes() {
    Provider provider = Provider.get(taskParams().providerUUID);
    if (provider != null) {
      Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      UniverseDefinitionTaskParams.UserIntent userIntent =
          u.getUniverseDetails().getPrimaryCluster().userIntent;
      return userIntent.numNodes;
    }
    return -1;
  }

  private String getK8sPropertyFromConfigOrDefault(
      Map<String, String> config,
      Map<String, String> regionConfig,
      Map<String, String> azConfig,
      String property,
      String defaultValue) {
    String value = azConfig != null ? azConfig.get(property) : null;
    if (value != null) {
      return value;
    }

    value = regionConfig != null ? regionConfig.get(property) : null;
    if (value != null) {
      return value;
    }

    value = config != null ? config.get(property) : null;
    if (value != null) {
      return value;
    }

    return defaultValue;
  }

  private String generateHelmOverride() {
    Map<String, Object> overrides = new HashMap<String, Object>();
    Yaml yaml = new Yaml();

    // TODO: decide if the user wants to expose all the services or just master.
    overrides = yaml.load(environment.resourceAsStream("k8s-expose-all.yml"));

    Provider provider = Provider.get(taskParams().providerUUID);
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(provider);
    Map<String, String> azConfig = new HashMap<String, String>();
    Map<String, String> regionConfig = new HashMap<String, String>();

    // Assert that universeDetails is provided to generate helm overrides.
    if (taskParams().universeDetails == null) {
      throw new RuntimeException("Universe details not provided to generate helm overrides");
    }
    UniverseDefinitionTaskParams taskUniverseDetails = taskParams().universeDetails;

    UniverseDefinitionTaskParams.Cluster cluster =
        taskParams().isReadOnlyCluster
            ? taskUniverseDetails.getReadOnlyClusters().get(0)
            : taskUniverseDetails.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;
    // TODO Support overriden instance types
    InstanceType instanceType =
        InstanceType.get(UUID.fromString(userIntent.provider), userIntent.instanceType);
    if (instanceType == null && !confGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
      log.info(
          "Config parameter {}", confGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources));
      log.error(
          "Unable to fetch InstanceType for {}, {}",
          userIntent.providerType,
          userIntent.instanceType);
      throw new RuntimeException(
          "Unable to fetch InstanceType "
              + userIntent.providerType
              + ": "
              + userIntent.instanceType);
    }

    int numNodes = 0, replicationFactorZone = 0, replicationFactor = 0;
    String placementCloud = null;
    String placementRegion = null;
    String placementZone = null;

    // This is true always now.
    boolean isMultiAz = (taskParams().masterAddresses != null) ? true : false;

    PlacementInfo pi;
    if (taskParams().isReadOnlyCluster) {
      pi =
          isMultiAz
              ? taskParams().placementInfo
              : taskUniverseDetails.getReadOnlyClusters().get(0).placementInfo;
    } else {
      pi =
          isMultiAz
              ? taskParams().placementInfo
              : taskUniverseDetails.getPrimaryCluster().placementInfo;
    }

    // To maintain backward compatability with old helm charts,
    // for Read cluster we need to pass isMultiAz=true. (By default isMultiAz is set to true as
    // masterAddr is never null at this point).
    // For primary cluster we still have to pass isMultiAz=false for single AZ so that we can create
    // PDB.
    // isMultiAz actually tells us if what we are deploying is partial universe.
    // Ex: If we want to deploy a universe in multiple AZs, each AZ is partial deployment.
    // If we want to deploy universe with both primary and read replicas in only one AZ each cluster
    // deployment is partial universe.
    if (!taskParams().isReadOnlyCluster) {
      isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    }
    if (pi != null) {
      if (pi.cloudList.size() != 0) {
        PlacementInfo.PlacementCloud cloud = pi.cloudList.get(0);
        placementCloud = cloud.code;
        if (cloud.regionList.size() != 0) {
          PlacementInfo.PlacementRegion region = cloud.regionList.get(0);
          placementRegion = region.code;
          if (region.azList.size() != 0) {
            PlacementInfo.PlacementAZ zone = null;
            if (isMultiAz && taskParams().azCode != null) {
              AvailabilityZone azByCode = AvailabilityZone.getByCode(provider, taskParams().azCode);
              if (azByCode == null) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format("Specified zone code %s does not exist.", taskParams().azCode));
              }
              Optional<PlacementInfo.PlacementAZ> pZone =
                  region.azList.stream()
                      .filter(azs -> azs.uuid.equals(azByCode.getUuid()))
                      .findFirst();
              if (!pZone.isPresent()) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format(
                        "Specified zone code %s does not exist in the placement config",
                        taskParams().azCode));
              }
              zone = pZone.get();
              placementZone = taskParams().azCode;
            } else {
              zone = region.azList.get(0);
              placementZone = AvailabilityZone.get(zone.uuid).getCode();
            }
            numNodes = zone.numNodesInAZ;
            replicationFactorZone = zone.replicationFactor;
            replicationFactor = userIntent.replicationFactor;
            Region r = Region.getOrBadRequest(region.uuid);
            regionConfig = CloudInfoInterface.fetchEnvVars(r);
            AvailabilityZone az = AvailabilityZone.getOrBadRequest(zone.uuid);
            azConfig = CloudInfoInterface.fetchEnvVars(az);
          }
        }
      }
    }

    Map<String, Object> storageOverrides =
        (HashMap) overrides.getOrDefault("storage", new HashMap<>());

    Map<String, Object> tserverDiskSpecs =
        (HashMap) storageOverrides.getOrDefault("tserver", new HashMap<>());
    Map<String, Object> masterDiskSpecs =
        (HashMap) storageOverrides.getOrDefault("master", new HashMap<>());
    // Override disk count and size for just the tserver pods according to user intent.
    if (userIntent.deviceInfo != null) {
      if (userIntent.deviceInfo.numVolumes != null) {
        tserverDiskSpecs.put("count", userIntent.deviceInfo.numVolumes);
      }
      if (userIntent.deviceInfo.volumeSize != null) {
        tserverDiskSpecs.put("size", String.format("%dGi", userIntent.deviceInfo.volumeSize));
      }
      // Storage class override applies to both tserver and master.
      if (userIntent.deviceInfo.storageClass != null) {
        tserverDiskSpecs.put("storageClass", userIntent.deviceInfo.storageClass);
        masterDiskSpecs.put("storageClass", userIntent.deviceInfo.storageClass);
      }
    }

    // Storage class needs to be updated if it is overriden in the zone config.
    String storageClass =
        getK8sPropertyFromConfigOrDefault(config, regionConfig, azConfig, "STORAGE_CLASS", null);
    if (StringUtils.isNoneEmpty(storageClass)) {
      tserverDiskSpecs.put("storageClass", storageClass);
      masterDiskSpecs.put("storageClass", storageClass);
    }

    if (isMultiAz) {
      overrides.put("masterAddresses", taskParams().masterAddresses);
      // Don't want to use the AZ tag on minikube since there are no AZ tags
      if (!environment.isDev()) {
        overrides.put("AZ", placementZone);
      }
      overrides.put("isMultiAz", true);
      if (taskParams().isReadOnlyCluster) {
        overrides.put("replicas", ImmutableMap.of("tserver", numNodes, "master", 0));
      } else {
        overrides.put(
            "replicas",
            ImmutableMap.of(
                "tserver",
                numNodes,
                "master",
                replicationFactorZone,
                "totalMasters",
                replicationFactor));
      }
    } else {
      if (taskParams().isReadOnlyCluster) {
        overrides.put("replicas", ImmutableMap.of("tserver", numNodes, "master", 0));
      } else {
        overrides.put(
            "replicas", ImmutableMap.of("tserver", numNodes, "master", replicationFactor));
      }
    }

    if (!tserverDiskSpecs.isEmpty()) {
      storageOverrides.put("tserver", tserverDiskSpecs);
    }

    // Override resource request and limit.
    Map<String, Object> tserverResource = new HashMap<>();
    Map<String, Object> tserverLimit = new HashMap<>();
    Map<String, Object> masterResource = new HashMap<>();
    Map<String, Object> masterLimit = new HashMap<>();

    if (!confGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
      // InstanceType should be defined here in this case.
      String instanceTypeCode = instanceType.getInstanceTypeCode();
      if (instanceTypeCode.equals("cloud")) {
        masterDiskSpecs.put("size", String.format("%dGi", 3));
      }
      if (!masterDiskSpecs.isEmpty()) {
        storageOverrides.put("master", masterDiskSpecs);
      }

      tserverResource.put("cpu", instanceType.getNumCores());
      tserverResource.put("memory", String.format("%.2fGi", instanceType.getMemSizeGB()));
      tserverLimit.put("cpu", instanceType.getNumCores() * burstVal);
      tserverLimit.put("memory", String.format("%.2fGi", instanceType.getMemSizeGB()));

      // If the instance type is not xsmall or dev, we would bump the master resource.
      if (!instanceTypeCode.equals("xsmall") && !instanceTypeCode.equals("dev")) {
        masterResource.put("cpu", 2);
        masterResource.put("memory", "4Gi");
        masterLimit.put("cpu", 2 * burstVal);
        masterLimit.put("memory", "4Gi");
      }
      // For testing with multiple deployments locally.
      if (instanceTypeCode.equals("dev")) {
        masterResource.put("cpu", 0.5);
        masterResource.put("memory", "0.5Gi");
        masterLimit.put("cpu", 0.5);
        masterLimit.put("memory", "0.5Gi");
      }
      // For cloud deployments, we want bigger bursts in CPU if available for better performance.
      // Memory should not be burstable as memory consumption above requests can lead to pods being
      // killed if the nodes is running out of resources.
      if (instanceTypeCode.equals("cloud")) {
        tserverLimit.put("cpu", instanceType.getNumCores() * 2);
        masterResource.put("cpu", 0.3);
        masterResource.put("memory", "1Gi");
        masterLimit.put("cpu", 0.6);
        masterLimit.put("memory", "1Gi");
      }
    } else {
      // Use defaults in case we don't find anythihng,
      if (userIntent.masterK8SNodeResourceSpec == null) {
        log.warn(
            "master k8s node resources (cpu/memory) passed as null for universe: "
                + taskUniverseDetails.getUniverseUUID()
                + " readcluster: "
                + taskParams().isReadOnlyCluster
                + " Using deafult values.");
        userIntent.masterK8SNodeResourceSpec = new UserIntent.K8SNodeResourceSpec();
      }
      if (userIntent.tserverK8SNodeResourceSpec == null) {
        log.warn(
            "tserver k8s node resources (cpu/memory) passed as null for universe: "
                + taskUniverseDetails.getUniverseUUID()
                + " readcluster: "
                + taskParams().isReadOnlyCluster
                + " Using deafult values.");
        userIntent.tserverK8SNodeResourceSpec = new UserIntent.K8SNodeResourceSpec();
      }
      masterResource.put(
          "memory", String.format("%.2f%s", userIntent.masterK8SNodeResourceSpec.memoryGib, "Gi"));
      masterResource.put(
          "cpu", String.format("%.2f", userIntent.masterK8SNodeResourceSpec.cpuCoreCount));
      tserverResource.put(
          "memory", String.format("%.2f%s", userIntent.tserverK8SNodeResourceSpec.memoryGib, "Gi"));
      tserverResource.put(
          "cpu", String.format("%.2f", userIntent.tserverK8SNodeResourceSpec.cpuCoreCount));
      // We are keeping requests and limits same.
      masterLimit.put(
          "memory", String.format("%.2f%s", userIntent.masterK8SNodeResourceSpec.memoryGib, "Gi"));
      masterLimit.put(
          "cpu", String.format("%.2f", userIntent.masterK8SNodeResourceSpec.cpuCoreCount));
      tserverLimit.put(
          "memory", String.format("%.2f%s", userIntent.tserverK8SNodeResourceSpec.memoryGib, "Gi"));
      tserverLimit.put(
          "cpu", String.format("%.2f", userIntent.tserverK8SNodeResourceSpec.cpuCoreCount));
    }

    Map<String, Object> resourceOverrides = new HashMap();
    if (!masterResource.isEmpty() && !masterLimit.isEmpty()) {
      resourceOverrides.put(
          "master",
          ImmutableMap.of(
              "requests", masterResource,
              "limits", masterLimit));
    }
    resourceOverrides.put(
        "tserver",
        ImmutableMap.of(
            "requests", tserverResource,
            "limits", tserverLimit));

    overrides.put("resource", resourceOverrides);

    Map<String, Object> imageInfo = new HashMap<>();
    // Override image tag based on ybsoftwareversion.
    String imageTag =
        taskParams().ybSoftwareVersion == null
            ? userIntent.ybSoftwareVersion
            : taskParams().ybSoftwareVersion;
    imageInfo.put("tag", imageTag);

    // Since the image registry will remain the same across differnet clusters,
    // it will always be at the provider level.
    if (config.containsKey("KUBECONFIG_IMAGE_REGISTRY")) {
      imageInfo.put("repository", config.get("KUBECONFIG_IMAGE_REGISTRY"));
    }
    if (config.containsKey("KUBECONFIG_IMAGE_PULL_SECRET_NAME")) {
      imageInfo.put("pullSecretName", config.get("KUBECONFIG_IMAGE_PULL_SECRET_NAME"));
    }
    overrides.put("Image", imageInfo);

    // Use primary cluster intent to read gflags, tls settings.
    UniverseDefinitionTaskParams.UserIntent primaryClusterIntent =
        taskUniverseDetails.getPrimaryCluster().userIntent;

    if (taskUniverseDetails.rootCA != null || taskUniverseDetails.getClientRootCA() != null) {
      Map<String, Object> tlsInfo = new HashMap<>();
      tlsInfo.put("enabled", true);
      tlsInfo.put("nodeToNode", primaryClusterIntent.enableNodeToNodeEncrypt);
      tlsInfo.put("clientToServer", primaryClusterIntent.enableClientToNodeEncrypt);
      tlsInfo.put("insecure", taskUniverseDetails.allowInsecure);
      String rootCert;
      String rootKey;
      if (taskUniverseDetails.rootCA != null) {
        rootCert = CertificateHelper.getCertPEM(taskUniverseDetails.rootCA);
        rootKey = CertificateHelper.getKeyPEM(taskUniverseDetails.rootCA);
      } else {
        rootCert = CertificateHelper.getCertPEM(taskUniverseDetails.getClientRootCA());
        rootKey = CertificateHelper.getKeyPEM(taskUniverseDetails.getClientRootCA());
      }

      if (rootKey != null && !rootKey.isEmpty()) {
        Map<String, Object> rootCA = new HashMap<>();
        rootCA.put("cert", rootCert);
        rootCA.put("key", rootKey);
        tlsInfo.put("rootCA", rootCA);
      } else {
        // In case root cert key is null which will be the case with Hashicorp Vault certificates
        // Generate wildcard node cert and client cert and set them in override file
        CertificateInfo certInfo;
        if (taskUniverseDetails.rootCA != null) {
          certInfo = CertificateInfo.get(taskUniverseDetails.rootCA);
        } else {
          certInfo = CertificateInfo.get(taskUniverseDetails.getClientRootCA());
        }

        Map<String, Object> rootCA = new HashMap<>();
        rootCA.put("cert", rootCert);
        rootCA.put("key", "");
        tlsInfo.put("rootCA", rootCA);
        String certManagerIssuer =
            getK8sPropertyFromConfigOrDefault(
                null, regionConfig, azConfig, "CERT-MANAGER-ISSUER", null);
        String certManagerClusterIssuer =
            getK8sPropertyFromConfigOrDefault(
                null, regionConfig, azConfig, "CERT-MANAGER-CLUSTERISSUER", null);

        if (certInfo.getCertType() == CertConfigType.K8SCertManager
            && (StringUtils.isNotEmpty(certManagerClusterIssuer)
                || StringUtils.isNotEmpty(certManagerIssuer))) {
          // User configuring a K8SCertManager type of certificate on a Universe and setting
          // the corresponding azConfig enables the cert-manager integration for this
          // Universe. The name of Issuer/ClusterIssuer will come from the azConfig.
          Map<String, Object> certManager = new HashMap<>();
          certManager.put("enabled", true);
          certManager.put("bootstrapSelfsigned", false);
          boolean useClusterIssuer = StringUtils.isNotEmpty(certManagerClusterIssuer);
          certManager.put("useClusterIssuer", useClusterIssuer);
          if (useClusterIssuer) {
            certManager.put("clusterIssuer", certManagerClusterIssuer);
          } else {
            certManager.put("issuer", certManagerIssuer);
          }
          tlsInfo.put("certManager", certManager);
        } else {
          CertificateProviderInterface certProvider =
              EncryptionInTransitUtil.getCertificateProviderInstance(
                  certInfo, runtimeConfigFactory.staticApplicationConf());
          // Generate node cert from cert provider and set nodeCert param
          // As we are using same node cert for all nodes, set wildcard commonName
          boolean newNamingStyle = taskUniverseDetails.useNewHelmNamingStyle;
          String kubeDomain =
              getK8sPropertyFromConfigOrDefault(
                  null, regionConfig, azConfig, "KUBE_DOMAIN", "cluster.local");
          List<String> dnsNames = getDnsNamesForSAN(newNamingStyle, kubeDomain);
          Map<String, Integer> subjectAltNames = new HashMap<>(dnsNames.size());
          for (String dnsName : dnsNames) {
            subjectAltNames.put(dnsName, GeneralName.dNSName);
          }
          CertificateDetails nodeCertDetails =
              certProvider.createCertificate(
                  null, dnsNames.get(0), null, null, null, null, subjectAltNames);
          Map<String, Object> nodeCert = new HashMap<>();
          nodeCert.put(
              "cert", Base64.getEncoder().encodeToString(nodeCertDetails.getCrt().getBytes()));
          nodeCert.put(
              "key", Base64.getEncoder().encodeToString(nodeCertDetails.getKey().getBytes()));
          tlsInfo.put("nodeCert", nodeCert);

          // Generate client cert from cert provider and set clientCert value
          CertificateDetails clientCertDetails =
              certProvider.createCertificate(null, "yugabyte", null, null, null, null, null);
          Map<String, Object> clientCert = new HashMap<>();
          clientCert.put(
              "cert", Base64.getEncoder().encodeToString(clientCertDetails.getCrt().getBytes()));
          clientCert.put(
              "key", Base64.getEncoder().encodeToString(clientCertDetails.getKey().getBytes()));
          tlsInfo.put("clientCert", clientCert);
        }
      }

      overrides.put("tls", tlsInfo);
    }
    if (primaryClusterIntent.enableIPV6) {
      overrides.put("ip_version_support", "v6_only");
    }

    UpdateStrategy updateStrategyParam = taskParams().updateStrategy;
    if (updateStrategyParam.equals(UpdateStrategy.RollingUpdate)) {
      Map<String, Object> partition = new HashMap<>();
      partition.put("tserver", taskParams().tserverPartition);
      partition.put("master", taskParams().masterPartition);
      overrides.put("partition", partition);
    } else if (updateStrategyParam.equals(UpdateStrategy.OnDelete)) {
      Map<String, Object> updateStrategy = new HashMap<>();
      updateStrategy.put("type", KubernetesCommandExecutor.UpdateStrategy.OnDelete.toString());
      overrides.put("updateStrategy", updateStrategy);
    }

    UUID placementUuid = cluster.uuid;
    Map<String, Object> gflagOverrides = new HashMap<>();
    // Go over master flags.
    Map<String, Object> masterOverrides =
        new HashMap<>(
            GFlagsUtil.getBaseGFlags(ServerType.MASTER, cluster, taskUniverseDetails.clusters));
    if (placementCloud != null && masterOverrides.get("placement_cloud") == null) {
      masterOverrides.put("placement_cloud", placementCloud);
    }
    if (placementRegion != null && masterOverrides.get("placement_region") == null) {
      masterOverrides.put("placement_region", placementRegion);
    }
    if (placementZone != null && masterOverrides.get("placement_zone") == null) {
      masterOverrides.put("placement_zone", placementZone);
    }
    if (placementUuid != null && masterOverrides.get("placement_uuid") == null) {
      masterOverrides.put("placement_uuid", placementUuid.toString());
    }
    if (taskUniverseDetails.xClusterInfo.isSourceRootCertDirPathGflagConfigured()) {
      masterOverrides.put(
          XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG,
          taskUniverseDetails.xClusterInfo.sourceRootCertDirPath);
    }
    if (!masterOverrides.isEmpty()) {
      gflagOverrides.put("master", masterOverrides);
    }

    // Go over tserver flags.
    Map<String, String> tserverOverrides =
        GFlagsUtil.getBaseGFlags(ServerType.TSERVER, cluster, taskUniverseDetails.clusters);
    if (!primaryClusterIntent
        .enableYSQL) { // In the UI, we can choose not to show these entries for read replica.
      tserverOverrides.put("enable_ysql", "false");
    }
    if (!primaryClusterIntent.enableYCQL) {
      tserverOverrides.put("start_cql_proxy", "false");
    }
    tserverOverrides.put("start_redis_proxy", String.valueOf(primaryClusterIntent.enableYEDIS));
    if (primaryClusterIntent.enableYSQL && primaryClusterIntent.enableYSQLAuth) {
      tserverOverrides.put("ysql_enable_auth", "true");
      Map<String, String> DEFAULT_YSQL_HBA_CONF_MAP =
          Collections.singletonMap(GFlagsUtil.YSQL_HBA_CONF_CSV, "local all yugabyte trust");
      if (tserverOverrides.containsKey(GFlagsUtil.YSQL_HBA_CONF_CSV)
          && confGetter.getGlobalConf(GlobalConfKeys.oidcFeatureEnhancements)) {
        /*
         * Preprocess the ysql_hba_conf_csv flag for IdP specific use case.
         * Refer Design Doc:
         * https://docs.google.com/document/d/1SJzZJrAqc0wkXTCuMS7UKi1-5xEuYQKCOOa3QWYpMeM/edit
         */
        GFlagsUtil.processHbaConfFlagIfRequired(
            null,
            tserverOverrides,
            confGetter,
            taskUniverseDetails.getUniverseUUID(),
            taskParams().isReadOnlyCluster
                ? taskUniverseDetails.getReadOnlyClusters().get(0).uuid
                : taskUniverseDetails.getPrimaryCluster().uuid);
      }
      GFlagsUtil.mergeCSVs(
          tserverOverrides, DEFAULT_YSQL_HBA_CONF_MAP, GFlagsUtil.YSQL_HBA_CONF_CSV);
      tserverOverrides.putIfAbsent(GFlagsUtil.YSQL_HBA_CONF_CSV, "local all yugabyte trust");
    }
    if (primaryClusterIntent.enableYCQL && primaryClusterIntent.enableYCQLAuth) {
      tserverOverrides.put("use_cassandra_authentication", "true");
    }
    if (placementCloud != null && tserverOverrides.get("placement_cloud") == null) {
      tserverOverrides.put("placement_cloud", placementCloud);
    }
    if (placementRegion != null && tserverOverrides.get("placement_region") == null) {
      tserverOverrides.put("placement_region", placementRegion);
    }
    if (placementZone != null && tserverOverrides.get("placement_zone") == null) {
      tserverOverrides.put("placement_zone", placementZone);
    }
    if (placementUuid != null && tserverOverrides.get("placement_uuid") == null) {
      tserverOverrides.put("placement_uuid", placementUuid.toString());
    }
    if (taskUniverseDetails.xClusterInfo.isSourceRootCertDirPathGflagConfigured()) {
      tserverOverrides.put(
          XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG,
          taskUniverseDetails.xClusterInfo.sourceRootCertDirPath);
    }
    if (!tserverOverrides.isEmpty()) {
      gflagOverrides.put("tserver", tserverOverrides);
    }

    if (!gflagOverrides.isEmpty()) {
      overrides.put("gflags", gflagOverrides);
    }

    String kubeDomain =
        getK8sPropertyFromConfigOrDefault(null, regionConfig, azConfig, "KUBE_DOMAIN", null);
    if (StringUtils.isNotBlank(kubeDomain)) {
      overrides.put("domainName", kubeDomain);
    }

    overrides.put("disableYsql", !primaryClusterIntent.enableYSQL);

    // If the value is anything else, that means the loadbalancer service by
    // default needed to be exposed.
    // NOTE: Will still be overriden from the provider level overrides.
    if (primaryClusterIntent.enableExposingService == ExposingServiceState.UNEXPOSED) {
      overrides.put("enableLoadBalancer", false);
    } else {
      // Even though the helm chart default is true, doing this from platform
      // just to make it explicit.
      overrides.put("enableLoadBalancer", true);
    }

    // For now the assumption is the all deployments will have the same kind of
    // loadbalancers, so the annotations will be at the provider level.
    // TODO (Arnav): Update this to use overrides created at the provider, region or
    // zone level.
    Map<String, Object> annotations;
    String overridesYAML =
        getK8sPropertyFromConfigOrDefault(config, regionConfig, azConfig, "OVERRIDES", null);

    Map<String, Object> ybcInfo = new HashMap<>();
    ybcInfo.put("enabled", taskParams().isEnableYbc());
    overrides.put("ybc", ybcInfo);

    if (overridesYAML != null) {
      annotations = yaml.load(overridesYAML);
      if (annotations != null) {
        HelmUtils.mergeYaml(overrides, annotations);
      }
    }
    ObjectMapper mapper = new ObjectMapper();
    String universeOverridesString = "", azOverridesString = "";
    try {
      if (taskParams().universeOverrides != null) {
        universeOverridesString = mapper.writeValueAsString(taskParams().universeOverrides);
        Map<String, Object> universeOverrides =
            mapper.readValue(universeOverridesString, Map.class);
        HelmUtils.mergeYaml(overrides, universeOverrides);
      }
      if (taskParams().azOverrides != null) {
        azOverridesString = mapper.writeValueAsString(taskParams().azOverrides);
        Map<String, Object> azOverrides = mapper.readValue(azOverridesString, Map.class);
        HelmUtils.mergeYaml(overrides, azOverrides);
      }
    } catch (IOException e) {
      log.error(
          String.format(
              "Error in writing overrides map to string or string to map: "
                  + "universe overrides: %s, azOverrides: %s",
              taskParams().universeOverrides, taskParams().azOverrides),
          e);
      throw new RuntimeException("Error in writing overrides map to string.");
    }
    // TODO gflags which have precedence over helm overrides should be merged here.

    validateOverrides(overrides);

    boolean helmLegacy =
        Universe.HelmLegacy.valueOf(taskParams().universeConfig.get(Universe.HELM2_LEGACY))
            == Universe.HelmLegacy.V2TO3;
    if (helmLegacy) {
      overrides.put("helm2Legacy", helmLegacy);
      Map<String, String> serviceToIP = getClusterIpForLoadBalancer();
      ArrayList<Object> serviceEndpoints = (ArrayList) overrides.get("serviceEndpoints");
      for (Object serviceEndpoint : serviceEndpoints) {
        Map<String, Object> endpoint = mapper.convertValue(serviceEndpoint, Map.class);
        String endpointName = (String) endpoint.get("name");
        if (serviceToIP.containsKey(endpointName)) {
          // With the newNamingStyle, the serviceToIP map will have
          // service names containing helmFullNameWithSuffix in
          // them. NOT making any changes to this code, as we have
          // deprecated Helm 2. And the newNamingStyle will be used
          // for newly created universes using Helm 3.
          endpoint.put("clusterIP", serviceToIP.get(endpointName));
        }
      }
    }

    // TODO(bhavin192): we can save universeDetails at the top, we use
    // this call a couple of times throughout this method.
    if (taskUniverseDetails.useNewHelmNamingStyle) {
      overrides.put("oldNamingStyle", false);
      overrides.put("fullnameOverride", taskParams().helmReleaseName);
    }

    try {
      Path tempFile =
          fileHelperService.createTempFile(taskParams().getUniverseUUID().toString(), ".yml");
      try (BufferedWriter bw = new BufferedWriter(new FileWriter(tempFile.toFile())); ) {
        yaml.dump(overrides, bw);
        return tempFile.toAbsolutePath().toString();
      }
    } catch (IOException e) {
      log.error(e.getMessage());
      throw new RuntimeException("Error writing Helm Override file!");
    }
  }

  /**
   * Construct the final form of Values file as helm would see it, and run any necessary validations
   * before it is applied.
   */
  private void validateOverrides(Map<String, Object> overrides) {
    // fetch the helm chart default values
    String defaultValuesStr =
        kubernetesManagerFactory
            .getManager()
            .helmShowValues(taskParams().ybSoftwareVersion, getConfig());
    if (defaultValuesStr != null) {
      Yaml defaultValuesYaml = new Yaml();
      Map<String, Object> defaultValues = defaultValuesYaml.load(defaultValuesStr);
      // apply overrides on the helm chart default values
      HelmUtils.mergeYaml(defaultValues, overrides);
      log.trace("Running validations on merged yaml: {}", defaultValues);
      // run any validations against the final values for helm install/upgrade
      // allow K8SCertManager cert type only with override
      allowK8SCertManagerOnlyWithOverride(defaultValues);
      // make sure certManager settings are provided
      ensureCertManagerSettings(defaultValues);
      // do not allow selfsignedBootstrap to be enabled ever from YBA
      preventBootstrapCA(defaultValues);
    }
  }

  /*
   * Do not allow certManager bootstrapSelfsigned, as YBA will not be able to
   * connect to the nodes. Instead, the YBA certificate should be used for TLS.
   */
  @SuppressWarnings("unchecked")
  private void preventBootstrapCA(Map<String, Object> values) {
    if (values.containsKey("tls")) {
      Map<String, Object> tlsInfo = (Map<String, Object>) values.get("tls");
      Boolean tlsEnabled = (Boolean) tlsInfo.getOrDefault("enabled", false);
      if (tlsEnabled && tlsInfo.containsKey("certManager")) {
        Map<String, Object> certManager = (Map<String, Object>) tlsInfo.get("certManager");
        Boolean certManagerEnabled = (Boolean) certManager.getOrDefault("enabled", false);
        Boolean bootstrapSelfsigned =
            (Boolean) certManager.getOrDefault("bootstrapSelfsigned", true);
        if (certManagerEnabled && bootstrapSelfsigned) {
          throw new RuntimeException("bootstrapSelfsigned is not supported");
        }
      }
    }
  }

  /*
   * Allow a K8SCertManager type of certificate to be used on a Universe only if
   * it also has the tls.certManager.enabled=true override. Note that when
   * K8SCertManager type of certificate is used, we automatically set
   * tls.certManager.enabled=true. However, the user could have specified an
   * explicit override setting it to false. This validation makes sure that the
   * user does not end up in such an unsupported configuration.
   */
  private void allowK8SCertManagerOnlyWithOverride(Map<String, Object> values) {
    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (u.getUniverseDetails().rootCA == null) {
      // nothing to validate on a Universe that does not have TLS enabled
      return;
    }
    CertificateInfo certInfo = CertificateInfo.get(u.getUniverseDetails().rootCA);
    boolean isK8SCertManager = certInfo.getCertType() == CertConfigType.K8SCertManager;
    boolean hasCertManagerOverride = hasCertManagerOverride(values);
    if (isK8SCertManager != hasCertManagerOverride) {
      throw new RuntimeException(
          "Use K8SCertManager type of certificate with the tls.certManager.enabled=true override.");
    }
  }

  @SuppressWarnings("unchecked")
  private boolean hasCertManagerOverride(Map<String, Object> values) {
    Map<String, Object> tlsInfo = (Map<String, Object>) values.getOrDefault("tls", null);
    if (tlsInfo != null) {
      Boolean tlsEnabled = (Boolean) tlsInfo.getOrDefault("enabled", false);
      if (tlsEnabled && tlsInfo.containsKey("certManager")) {
        Map<String, Object> certManager = (Map<String, Object>) tlsInfo.get("certManager");
        return (Boolean) certManager.getOrDefault("enabled", false);
      }
    }
    return false;
  }

  @SuppressWarnings("unchecked")
  private void ensureCertManagerSettings(Map<String, Object> values) {
    if (hasCertManagerOverride(values)) {
      // make sure useClusterIssuer and the appropriate issuer/clusterissuer name is
      // provided
      Map<String, Object> certManager =
          (Map<String, Object>) ((Map<String, Object>) values.get("tls")).get("certManager");
      if (!certManager.containsKey("useClusterIssuer")) {
        throw new RuntimeException(
            "useClusterIssuer is required when tls.certManager.enabled=true");
      }
      Boolean useClusterIssuer = (Boolean) certManager.get("useClusterIssuer");
      if (useClusterIssuer == null) {
        throw new RuntimeException(
            "useClusterIssuer is required when tls.certManager.enabled=true");
      }
      if (useClusterIssuer) {
        String clusterIssuerName = (String) certManager.getOrDefault("clusterIssuer", "");
        if (clusterIssuerName.isEmpty()) {
          throw new RuntimeException("clusterIssuer is required when useClusterIssuer=true");
        }
      } else {
        String issuerName = (String) certManager.getOrDefault("issuer", "");
        if (issuerName.isEmpty()) {
          throw new RuntimeException("issuer is required when useClusterIssuer=false");
        }
      }
    }
  }

  private List<String> getDnsNamesForSAN(boolean newNamingStyle, String kubeDomain) {
    List<String> dnsNames = new ArrayList<>(4);
    if (newNamingStyle) {
      dnsNames.add(
          String.format(
              "*.%s-yb-tservers.%s", taskParams().helmReleaseName, taskParams().namespace));
      dnsNames.add(
          String.format(
              "*.%s-yb-tservers.%s.svc.%s",
              taskParams().helmReleaseName, taskParams().namespace, kubeDomain));
      dnsNames.add(
          String.format(
              "*.%s-yb-masters.%s", taskParams().helmReleaseName, taskParams().namespace));
      dnsNames.add(
          String.format(
              "*.%s-yb-masters.%s.svc.%s",
              taskParams().helmReleaseName, taskParams().namespace, kubeDomain));
    } else {
      dnsNames.add(String.format("*.yb-tservers.%s", taskParams().namespace));
      dnsNames.add(String.format("*.yb-tservers.%s.svc.%s", taskParams().namespace, kubeDomain));
      dnsNames.add(String.format("*.yb-masters.%s", taskParams().namespace));
      dnsNames.add(String.format("*.yb-masters.%s.svc.%s", taskParams().namespace, kubeDomain));
    }
    return dnsNames;
  }
}
