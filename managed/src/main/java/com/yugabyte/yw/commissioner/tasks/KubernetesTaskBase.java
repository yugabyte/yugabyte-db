// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckNumPod;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public abstract class KubernetesTaskBase extends UniverseDefinitionTaskBase {

  protected boolean isBlacklistLeaders = false;
  protected int leaderBacklistWaitTimeMs;

  @Inject
  protected KubernetesTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class KubernetesPlacement {
    public PlacementInfo placementInfo;
    public Map<UUID, Integer> masters;
    public Map<UUID, Integer> tservers;
    public Map<UUID, Map<String, String>> configs;

    public KubernetesPlacement(PlacementInfo pi) {
      placementInfo = pi;
      masters = PlacementInfoUtil.getNumMasterPerAZ(pi);
      tservers = PlacementInfoUtil.getNumTServerPerAZ(pi);
      // Mapping of the deployment zone and its corresponding Kubeconfig.
      configs = PlacementInfoUtil.getConfigPerAZ(pi);
    }
  }

  public void createPodsTask(
      KubernetesPlacement placement, String masterAddresses, boolean isReadOnlyCluster) {
    createPodsTask(placement, masterAddresses, null, null, new PlacementInfo(), isReadOnlyCluster);
  }

  public void createPodsTask(
      KubernetesPlacement newPlacement,
      String masterAddresses,
      KubernetesPlacement currPlacement,
      ServerType serverType,
      PlacementInfo activeZones,
      boolean isReadOnlyCluster) {
    String ybSoftwareVersion;
    if (isReadOnlyCluster) {
      ybSoftwareVersion = taskParams().getReadOnlyClusters().get(0).userIntent.ybSoftwareVersion;
      String primaryClusterVersion =
          Universe.getOrBadRequest(taskParams().universeUUID)
              .getUniverseDetails()
              .getPrimaryCluster()
              .userIntent
              .ybSoftwareVersion;
      if (!primaryClusterVersion.equals(ybSoftwareVersion)) {
        String msg =
            String.format(
                "Read cluster software version %s is not matching with"
                    + " primary cluster software version %s",
                ybSoftwareVersion, primaryClusterVersion);
        log.error(msg);
        throw new IllegalArgumentException(msg);
      }
    } else {
      ybSoftwareVersion = taskParams().getPrimaryCluster().userIntent.ybSoftwareVersion;
    }

    boolean edit = currPlacement != null;

    Provider provider;
    if (isReadOnlyCluster) {
      provider =
          Provider.getOrBadRequest(
              UUID.fromString(taskParams().getReadOnlyClusters().get(0).userIntent.provider));
    } else {
      provider =
          Provider.getOrBadRequest(
              UUID.fromString(taskParams().getPrimaryCluster().userIntent.provider));
    }
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    Map<UUID, Map<String, String>> activeDeploymentConfigs =
        PlacementInfoUtil.getConfigPerAZ(activeZones);
    // Only used for new deployments, so maybe empty.
    SubTaskGroup createNamespaces =
        getTaskExecutor()
            .createSubTaskGroup(
                KubernetesCommandExecutor.CommandType.CREATE_NAMESPACE.getSubTaskGroupName(),
                executor);
    createNamespaces.setSubTaskGroupType(SubTaskGroupType.Provisioning);

    SubTaskGroup applySecrets =
        getTaskExecutor()
            .createSubTaskGroup(
                KubernetesCommandExecutor.CommandType.APPLY_SECRET.getSubTaskGroupName(), executor);
    applySecrets.setSubTaskGroupType(SubTaskGroupType.Provisioning);

    SubTaskGroup helmInstalls =
        getTaskExecutor()
            .createSubTaskGroup(
                KubernetesCommandExecutor.CommandType.HELM_INSTALL.getSubTaskGroupName(), executor);
    helmInstalls.setSubTaskGroupType(SubTaskGroupType.Provisioning);

    SubTaskGroup podsWait =
        getTaskExecutor()
            .createSubTaskGroup(
                KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS.getSubTaskGroupName(), executor);
    podsWait.setSubTaskGroupType(SubTaskGroupType.Provisioning);

    for (Entry<UUID, Map<String, String>> entry : newPlacement.configs.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).code : null;

      if (!newPlacement.masters.containsKey(azUUID) && serverType == ServerType.MASTER) {
        continue;
      }

      Map<String, String> config = entry.getValue();

      PlacementInfo tempPI = new PlacementInfo();
      PlacementInfoUtil.addPlacementZone(azUUID, tempPI);

      int currNumTservers = 0, currNumMasters = 0;
      int newNumMasters = newPlacement.masters.getOrDefault(azUUID, 0);
      int newNumTservers = newPlacement.tservers.getOrDefault(azUUID, 0);

      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = newNumTservers;
      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor = newNumMasters;

      if (edit) {
        currNumMasters = currPlacement.masters.getOrDefault(azUUID, 0);
        currNumTservers = currPlacement.tservers.getOrDefault(azUUID, 0);
        // If the num pods are now less, we do not want to add anything,
        // so we skip.
        if (serverType == ServerType.MASTER) {
          if (newNumMasters <= currNumMasters) {
            continue;
          }
          // When adding new masters, we want to not increase the number of tservers
          // in the same operation.
          tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = currNumTservers;
        } else {
          if (newNumTservers <= currNumTservers) {
            continue;
          }
        }
      }

      // This will always be false in the case of a new universe.
      if (activeDeploymentConfigs.containsKey(azUUID)) {
        // Helm Upgrade
        // Potential changes:
        // 1) Adding masters: Do not want either old masters or tservers to be rolled.
        // 2) Adding tservers:
        //    a) No masters changed, that means the master addresses are the same. Do not need
        //       to set partition on tserver or master.
        //    b) Masters changed, that means the master addresses changed, and we don't want to
        //       roll the older pods (or the new masters, since they will be in shell mode).
        int tserverPartition = currNumMasters != newNumMasters ? currNumTservers : 0;
        int masterPartition =
            currNumMasters != newNumMasters
                ? (serverType == ServerType.MASTER ? currNumMasters : newNumMasters)
                : 0;
        helmInstalls.addSubTask(
            createKubernetesExecutorTaskForServerType(
                CommandType.HELM_UPGRADE,
                tempPI,
                azCode,
                masterAddresses,
                ybSoftwareVersion,
                serverType,
                config,
                masterPartition,
                tserverPartition,
                isReadOnlyCluster));

        // When adding masters, the number of tservers will be still the same as before.
        // They get added later.
        int podsToWaitFor =
            serverType == ServerType.MASTER
                ? newNumMasters + currNumTservers
                : newNumMasters + newNumTservers;
        podsWait.addSubTask(
            createKubernetesCheckPodNumTask(
                KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS,
                azCode,
                config,
                podsToWaitFor,
                isReadOnlyCluster));
      } else {
        // Don't create the namespace if user has provided
        // KUBENAMESPACE value, as we might not have access to list or
        // create namespaces in such cases.
        if (config.get("KUBENAMESPACE") == null) {
          // Create the namespaces of the deployment.
          createNamespaces.addSubTask(
              createKubernetesExecutorTask(
                  KubernetesCommandExecutor.CommandType.CREATE_NAMESPACE,
                  azCode,
                  config,
                  isReadOnlyCluster));
        }

        // Apply the necessary pull secret to each namespace.
        applySecrets.addSubTask(
            createKubernetesExecutorTask(
                KubernetesCommandExecutor.CommandType.APPLY_SECRET,
                azCode,
                config,
                isReadOnlyCluster));

        // Create the helm deployments.
        helmInstalls.addSubTask(
            createKubernetesExecutorTask(
                KubernetesCommandExecutor.CommandType.HELM_INSTALL,
                tempPI,
                azCode,
                masterAddresses,
                ybSoftwareVersion,
                config,
                isReadOnlyCluster));

        // Add zone to active configs.
        PlacementInfoUtil.addPlacementZone(azUUID, activeZones);
      }
    }

    getRunnableTask().addSubTaskGroup(createNamespaces);
    getRunnableTask().addSubTaskGroup(applySecrets);
    getRunnableTask().addSubTaskGroup(helmInstalls);
    getRunnableTask().addSubTaskGroup(podsWait);
  }

  public void upgradePodsTask(
      KubernetesPlacement newPlacement,
      String masterAddresses,
      KubernetesPlacement currPlacement,
      ServerType serverType,
      String softwareVersion,
      int waitTime,
      boolean masterChanged,
      boolean tserverChanged,
      boolean newNamingStyle) {

    boolean edit = currPlacement != null;
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(taskParams().getPrimaryCluster().userIntent.provider));
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    String nodePrefix = taskParams().nodePrefix;

    Map<UUID, Integer> serversToUpdate =
        serverType == ServerType.MASTER ? newPlacement.masters : newPlacement.tservers;

    if (serverType == ServerType.TSERVER) {
      Map<UUID, PlacementInfo.PlacementAZ> placementAZMap =
          PlacementInfoUtil.getPlacementAZMap(newPlacement.placementInfo);

      List<UUID> sortedZonesToUpdate =
          serversToUpdate
              .keySet()
              .stream()
              .sorted(Comparator.comparing(zoneUUID -> !placementAZMap.get(zoneUUID).isAffinitized))
              .collect(Collectors.toList());

      // Put isAffinitized availability zones first
      serversToUpdate =
          sortedZonesToUpdate
              .stream()
              .collect(
                  Collectors.toMap(
                      Function.identity(), serversToUpdate::get, (a, b) -> a, LinkedHashMap::new));
    }

    if (serverType == ServerType.TSERVER && isBlacklistLeaders && !edit) {
      // clear blacklist
      List<NodeDetails> tServerNodes = getUniverse().getTServers();
      createModifyBlackListTask(tServerNodes, false /* isAdd */, true /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    for (Entry<UUID, Integer> entry : serversToUpdate.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).code : null;

      PlacementInfo tempPI = new PlacementInfo();
      PlacementInfoUtil.addPlacementZone(azUUID, tempPI);

      int currNumMasters = 0, currNumTservers = 0;
      int newNumMasters = newPlacement.masters.getOrDefault(azUUID, 0);
      int newNumTservers = newPlacement.tservers.getOrDefault(azUUID, 0);

      int numPods = serverType == ServerType.MASTER ? newNumMasters : newNumTservers;

      if (edit) {
        currNumMasters = currPlacement.masters.getOrDefault(azUUID, 0);
        currNumTservers = currPlacement.tservers.getOrDefault(azUUID, 0);
        if (serverType == ServerType.TSERVER) {
          // Since we only want to roll the old pods and not the new ones.
          numPods = newNumTservers > currNumTservers ? currNumTservers : newNumTservers;
          if (currNumTservers == 0) {
            continue;
          }
        }
      }

      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = newNumTservers;
      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor = newNumMasters;

      Map<String, String> config = newPlacement.configs.get(azUUID);

      // Upgrade the master pods individually for each deployment.
      for (int partition = numPods - 1; partition >= 0; partition--) {
        // Possible scenarios:
        // 1) Upgrading masters.
        //    a) The tservers have no changes. In that case, the tserver partition should
        //       be 0.
        //    b) The tservers have changes. In the case of an edit, the value should be
        //       the old number of tservers (since the new will already have the updated values).
        //       Otherwise, the value should be the number of existing pods (since we don't
        //       want any pods to be rolled.)
        // 2) Upgrading the tserver. In this case, the masters will always have already rolled.
        //    So it is safe to assume for all current supported operations via upgrade,
        //    this will be a no-op. But for future proofing, we try to model it the same way
        //    as the tservers.
        int masterPartition = masterChanged ? (edit ? currNumMasters : newNumMasters) : 0;
        int tserverPartition = tserverChanged ? (edit ? currNumTservers : newNumTservers) : 0;
        masterPartition = serverType == ServerType.MASTER ? partition : masterPartition;
        tserverPartition = serverType == ServerType.TSERVER ? partition : tserverPartition;

        NodeDetails node =
            getKubernetesNodeName(
                partition,
                azCode,
                serverType,
                isMultiAz, /*isReadOnlyCluster*/
                false); // TODO handle readonly cluster
        boolean isLeaderBlacklistValidRF = isLeaderBlacklistValidRF(node.nodeName);
        List<NodeDetails> nodeList = new ArrayList<>();
        nodeList.add(node);
        if (serverType == ServerType.TSERVER
            && isBlacklistLeaders
            && isLeaderBlacklistValidRF
            && !edit) {
          createModifyBlackListTask(
                  Arrays.asList(node), true /* isAdd */, true /* isLeaderBlacklist */)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          createWaitForLeaderBlacklistCompletionTask(leaderBacklistWaitTimeMs)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        }

        createSingleKubernetesExecutorTaskForServerType(
            CommandType.HELM_UPGRADE,
            tempPI,
            azCode,
            masterAddresses,
            softwareVersion,
            serverType,
            config,
            masterPartition,
            tserverPartition,
            /*isReadOnlyCluster*/ false);
        String podName =
            getPodName(partition, azCode, serverType, nodePrefix, isMultiAz, newNamingStyle);
        createKubernetesWaitForPodTask(
            KubernetesWaitForPod.CommandType.WAIT_FOR_POD,
            podName,
            azCode,
            config,
            /*isReadOnlyCluster*/ false);

        createWaitForServersTasks(nodeList, serverType)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        createWaitForServerReady(node, serverType, waitTime)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        if (serverType == ServerType.TSERVER
            && isBlacklistLeaders
            && isLeaderBlacklistValidRF
            && !edit) {
          createModifyBlackListTask(
                  Arrays.asList(node), false /* isAdd */, true /* isLeaderBlacklist */)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        }
      }
    }
  }

  public void deletePodsTask(
      KubernetesPlacement currPlacement,
      String masterAddresses,
      KubernetesPlacement newPlacement,
      boolean userIntentChange,
      Provider provider) {

    String ybSoftwareVersion = taskParams().getPrimaryCluster().userIntent.ybSoftwareVersion;

    boolean edit = newPlacement != null;
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

    // If no config in new placement, delete deployment.
    SubTaskGroup helmDeletes =
        getTaskExecutor()
            .createSubTaskGroup(
                KubernetesCommandExecutor.CommandType.HELM_DELETE.getSubTaskGroupName(), executor);
    helmDeletes.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

    SubTaskGroup volumeDeletes =
        getTaskExecutor()
            .createSubTaskGroup(
                KubernetesCommandExecutor.CommandType.VOLUME_DELETE.getSubTaskGroupName(),
                executor);
    volumeDeletes.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

    SubTaskGroup namespaceDeletes =
        getTaskExecutor()
            .createSubTaskGroup(
                KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE.getSubTaskGroupName(),
                executor);
    namespaceDeletes.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

    SubTaskGroup podsWait =
        getTaskExecutor()
            .createSubTaskGroup(
                KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS.getSubTaskGroupName(), executor);
    podsWait.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

    for (Entry<UUID, Map<String, String>> entry : currPlacement.configs.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).code : null;
      Map<String, String> config = entry.getValue();

      // If the new placement also has the AZ, we need to scale down. But if there
      // was a change in the instance type, the updateRemainingPod itself would have taken care
      // of the deployments' scale down.

      boolean keepDeployment = false;
      if (edit) {
        keepDeployment = newPlacement.configs.containsKey(azUUID) && !userIntentChange;
      }
      if (keepDeployment) {
        PlacementInfo tempPI = new PlacementInfo();
        PlacementInfoUtil.addPlacementZone(azUUID, tempPI);
        tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ =
            newPlacement.tservers.get(azUUID);
        tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor =
            newPlacement.masters.getOrDefault(azUUID, 0);
        helmDeletes.addSubTask(
            createKubernetesExecutorTask(
                CommandType.HELM_UPGRADE,
                tempPI,
                azCode,
                masterAddresses,
                ybSoftwareVersion,
                config,
                /*isReadOnlyCluster*/ false));
        podsWait.addSubTask(
            createKubernetesCheckPodNumTask(
                KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS,
                azCode,
                config,
                newPlacement.tservers.get(azUUID) + newPlacement.masters.getOrDefault(azUUID, 0),
                /*isReadOnlyCluster*/ false));
      } else {
        // Delete the helm deployments.
        helmDeletes.addSubTask(
            createKubernetesExecutorTask(
                KubernetesCommandExecutor.CommandType.HELM_DELETE, azCode, config, false));

        // Delete the PVs created for the deployments.
        volumeDeletes.addSubTask(
            createKubernetesExecutorTask(
                KubernetesCommandExecutor.CommandType.VOLUME_DELETE, azCode, config, false));

        // If the namespace is configured at the AZ, we don't delete
        // it, as it is not created by us.
        if (config.get("KUBENAMESPACE") == null) {
          // Delete the namespaces of the deployments.
          namespaceDeletes.addSubTask(
              createKubernetesExecutorTask(
                  KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE, azCode, config, false));
        }
      }
    }
    getRunnableTask().addSubTaskGroup(helmDeletes);
    getRunnableTask().addSubTaskGroup(volumeDeletes);
    getRunnableTask().addSubTaskGroup(namespaceDeletes);
    getRunnableTask().addSubTaskGroup(podsWait);
  }

  // TODO(bhavin192): should we just override the getNodeName from
  // UniverseDefinitionTaskBase?
  /*
  Returns the NodeDetails of the pod that we need to wait for.
  */
  public NodeDetails getKubernetesNodeName(
      int partition,
      String azCode,
      ServerType serverType,
      boolean isMultiAz,
      boolean isReadCluster) {
    String sType = serverType == ServerType.MASTER ? "yb-master" : "yb-tserver";
    String nodeName =
        isMultiAz
            ? String.format("%s-%d_%s", sType, partition, azCode)
            : String.format("%s-%d", sType, partition);
    nodeName = isReadCluster ? String.format("%s%s", nodeName, Universe.READONLY) : nodeName;
    NodeDetails node = new NodeDetails();
    node.nodeName = nodeName;
    return node;
  }

  public String getPodName(
      int partition,
      String azCode,
      ServerType serverType,
      String nodePrefix,
      boolean isMultiAz,
      boolean newNamingStyle) {
    String sType = serverType == ServerType.MASTER ? "yb-master" : "yb-tserver";
    String helmFullName =
        PlacementInfoUtil.getHelmFullNameWithSuffix(isMultiAz, nodePrefix, azCode, newNamingStyle);
    return String.format("%s%s-%d", helmFullName, sType, partition);
  }

  /*
  Sends a collection of all the pods that need to be added.
  */
  public Set<NodeDetails> getPodsToAdd(
      Map<UUID, Integer> newPlacement,
      Map<UUID, Integer> currPlacement,
      ServerType serverType,
      boolean isMultiAz,
      boolean isReadCluster) {

    Set<NodeDetails> podsToAdd = new HashSet<NodeDetails>();
    for (Entry<UUID, Integer> entry : newPlacement.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = AvailabilityZone.get(azUUID).code;
      int numNewReplicas = entry.getValue();
      int numCurrReplicas = 0;
      if (currPlacement != null) {
        numCurrReplicas = currPlacement.getOrDefault(azUUID, 0);
      }
      for (int i = numCurrReplicas; i < numNewReplicas; i++) {
        NodeDetails node = getKubernetesNodeName(i, azCode, serverType, isMultiAz, isReadCluster);
        podsToAdd.add(node);
      }
    }
    return podsToAdd;
  }

  /*
  Sends a collection of all the pods that need to be removed.
  */
  public Set<NodeDetails> getPodsToRemove(
      Map<UUID, Integer> newPlacement,
      Map<UUID, Integer> currPlacement,
      ServerType serverType,
      Universe universe,
      boolean isMultiAz,
      boolean isReadCluster) {
    Set<NodeDetails> podsToRemove = new HashSet<NodeDetails>();
    for (Entry<UUID, Integer> entry : currPlacement.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = AvailabilityZone.get(azUUID).code;
      int numCurrReplicas = entry.getValue();
      int numNewReplicas = newPlacement.getOrDefault(azUUID, 0);
      for (int i = numCurrReplicas - 1; i >= numNewReplicas; i--) {
        NodeDetails node = getKubernetesNodeName(i, azCode, serverType, isMultiAz, isReadCluster);
        podsToRemove.add(universe.getNode(node.nodeName));
      }
    }
    return podsToRemove;
  }

  // Create Kubernetes Executor task for creating the namespaces and pull secrets.
  public KubernetesCommandExecutor createKubernetesExecutorTask(
      KubernetesCommandExecutor.CommandType commandType,
      String az,
      Map<String, String> config,
      boolean isReadOnlyCluster) {
    return createKubernetesExecutorTask(
        commandType, null, az, null, null, config, isReadOnlyCluster);
  }

  // Create the Kubernetes Executor task for the helm deployments. (USED)
  public KubernetesCommandExecutor createKubernetesExecutorTask(
      CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      Map<String, String> config,
      boolean isReadOnlyCluster) {
    return createKubernetesExecutorTaskForServerType(
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        ServerType.EITHER,
        config,
        0 /* master partition */,
        0 /* tserver partition */,
        isReadOnlyCluster);
  }

  // Create and return the Kubernetes Executor task for deployment of a k8s universe.
  public KubernetesCommandExecutor createKubernetesExecutorTaskForServerType(
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      boolean isReadOnlyCluster) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    UniverseDefinitionTaskParams.Cluster primary;
    if (isReadOnlyCluster) {
      primary =
          Universe.getOrBadRequest(taskParams().universeUUID)
              .getUniverseDetails()
              .getPrimaryCluster();
      params.providerUUID =
          UUID.fromString(taskParams().getReadOnlyClusters().get(0).userIntent.provider);
    } else {
      primary = taskParams().getPrimaryCluster();
      params.providerUUID = UUID.fromString(primary.userIntent.provider);
    }

    params.commandType = commandType;
    params.nodePrefix = taskParams().nodePrefix;
    params.universeUUID = taskParams().universeUUID;

    if (az != null) {
      params.nodePrefix = String.format("%s-%s", params.nodePrefix, az);
    }
    if (masterAddresses != null) {
      params.masterAddresses = masterAddresses;
    }
    if (ybSoftwareVersion != null) {
      params.ybSoftwareVersion = ybSoftwareVersion;
    }
    if (pi != null) {
      params.placementInfo = pi;
    }
    if (config != null) {
      params.config = config;
      // This assumes that the config is az config.
      // params.namespace remains null if config is not passed.
      params.namespace =
          PlacementInfoUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    params.masterPartition = masterPartition;
    params.tserverPartition = tserverPartition;
    params.enableNodeToNodeEncrypt = primary.userIntent.enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = primary.userIntent.enableClientToNodeEncrypt;
    params.serverType = serverType;
    params.isReadOnlyCluster = isReadOnlyCluster;
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    return task;
  }

  public void createSingleKubernetesExecutorTask(
      KubernetesCommandExecutor.CommandType commandType, boolean isReadCluster) {
    createSingleKubernetesExecutorTask(commandType, null, isReadCluster);
  }

  // Create a single Kubernetes Executor task in case we cannot execute tasks in parallel.
  public void createSingleKubernetesExecutorTask(
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      boolean isReadOnlyCluster) {
    createSingleKubernetesExecutorTaskForServerType(
        commandType,
        pi,
        null,
        null,
        null,
        ServerType.EITHER,
        null,
        0 /* master partition */,
        0 /* tserver partition */,
        isReadOnlyCluster);
  }

  // Create a single Kubernetes Executor task in case we cannot execute tasks in parallel.
  public void createSingleKubernetesExecutorTaskForServerType(
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      boolean isReadOnlyCluster) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup(commandType.getSubTaskGroupName(), executor);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    UniverseDefinitionTaskParams.Cluster primary;
    if (isReadOnlyCluster) {
      primary =
          Universe.getOrBadRequest(taskParams().universeUUID)
              .getUniverseDetails()
              .getPrimaryCluster();
      params.providerUUID =
          UUID.fromString(taskParams().getReadOnlyClusters().get(0).userIntent.provider);
    } else {
      primary = taskParams().getPrimaryCluster();
      params.providerUUID = UUID.fromString(primary.userIntent.provider);
    }
    params.commandType = commandType;
    params.nodePrefix = taskParams().nodePrefix;
    params.universeUUID = taskParams().universeUUID;

    if (az != null) {
      params.nodePrefix = String.format("%s-%s", params.nodePrefix, az);
    }
    if (masterAddresses != null) {
      params.masterAddresses = masterAddresses;
    }
    if (ybSoftwareVersion != null) {
      params.ybSoftwareVersion = ybSoftwareVersion;
    }
    if (pi != null) {
      params.placementInfo = pi;
    }
    if (config != null) {
      params.config = config;
      // This assumes that the config is az config.
      // params.namespace remains null if config is not passed.
      params.namespace =
          PlacementInfoUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    params.masterPartition = masterPartition;
    params.tserverPartition = tserverPartition;
    params.enableNodeToNodeEncrypt = primary.userIntent.enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = primary.userIntent.enableClientToNodeEncrypt;
    params.serverType = serverType;
    params.isReadOnlyCluster = isReadOnlyCluster;
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
  }

  public void createKubernetesWaitForPodTask(
      KubernetesWaitForPod.CommandType commandType,
      String podName,
      String az,
      Map<String, String> config,
      boolean isReadOnlyCluster) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup(commandType.getSubTaskGroupName(), executor);
    KubernetesWaitForPod.Params params = new KubernetesWaitForPod.Params();
    UniverseDefinitionTaskParams.Cluster primary = taskParams().getPrimaryCluster();
    params.providerUUID = UUID.fromString(primary.userIntent.provider);
    params.commandType = commandType;
    params.nodePrefix = taskParams().nodePrefix;

    if (az != null) {
      params.nodePrefix = String.format("%s-%s", params.nodePrefix, az);
    }
    if (config != null) {
      params.config = config;
      // This assumes that the config is az config.
      // params.namespace remains null if config is not passed.
      params.namespace =
          PlacementInfoUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    params.universeUUID = taskParams().universeUUID;
    params.podName = podName;
    KubernetesWaitForPod task = createTask(KubernetesWaitForPod.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.KubernetesWaitForPod);
  }

  public KubernetesCheckNumPod createKubernetesCheckPodNumTask(
      KubernetesCheckNumPod.CommandType commandType,
      String az,
      Map<String, String> config,
      int numPods,
      boolean isReadOnlyCluster) {
    KubernetesCheckNumPod.Params params = new KubernetesCheckNumPod.Params();
    UniverseDefinitionTaskParams.Cluster primary;
    if (isReadOnlyCluster) {
      primary =
          Universe.getOrBadRequest(taskParams().universeUUID)
              .getUniverseDetails()
              .getPrimaryCluster();
    } else {
      primary = taskParams().getPrimaryCluster();
    }
    params.providerUUID = UUID.fromString(primary.userIntent.provider);
    params.commandType = commandType;
    params.nodePrefix = taskParams().nodePrefix;
    if (az != null) {
      params.nodePrefix = String.format("%s-%s", params.nodePrefix, az);
    }
    if (config != null) {
      params.config = config;
      // This assumes that the config is az config.
      // params.namespace remains null if config is not passed.
      params.namespace =
          PlacementInfoUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    params.universeUUID = taskParams().universeUUID;
    params.podNum = numPods;
    KubernetesCheckNumPod task = createTask(KubernetesCheckNumPod.class);
    task.initialize(params);
    return task;
  }
}
