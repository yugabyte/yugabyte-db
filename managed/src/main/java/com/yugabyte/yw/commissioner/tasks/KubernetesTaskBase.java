// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.HandleKubernetesNamespacedServices;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckNumPod;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.commissioner.tasks.subtasks.ValidateNodeDiskSize;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesPartitions;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.RollMaxBatchSize;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.MetricSourceState;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import java.io.IOException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.Builder;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
public abstract class KubernetesTaskBase extends UniverseDefinitionTaskBase {

  protected int leaderBacklistWaitTimeMs;
  public static final String K8S_NODE_YW_DATA_DIR = "/mnt/disk0/yw-data";
  protected final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected KubernetesTaskBase(BaseTaskDependencies baseTaskDependencies) {
    this(baseTaskDependencies, null /* kubernetesManagerFactory */);
  }

  @Inject
  protected KubernetesTaskBase(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies);
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  @Override
  protected boolean isBlacklistLeaders() {
    return false; // TODO: Modify blacklist is disabled by default for k8s now for some reason.
  }

  protected boolean isSkipPrechecks() {
    return false;
  }

  public static class KubernetesPlacement {
    public PlacementInfo placementInfo;
    public Map<UUID, Integer> masters;
    public Map<UUID, Integer> tservers;
    public Map<UUID, Map<String, String>> configs;

    public KubernetesPlacement(PlacementInfo pi, boolean isReadOnlyCluster) {
      placementInfo = pi;
      masters = isReadOnlyCluster ? new HashMap<>() : PlacementInfoUtil.getNumMasterPerAZ(pi);
      tservers = PlacementInfoUtil.getNumTServerPerAZ(pi);
      // Mapping of the deployment zone and its corresponding Kubeconfig.
      configs = KubernetesUtil.getConfigPerAZ(pi);
    }
  }

  // TODO: move all varargs here PLAT-15007
  @Builder
  public static class PodUpgradeParams {
    public static PodUpgradeParams DEFAULT = PodUpgradeParams.builder().build();
    @Builder.Default public RollMaxBatchSize rollMaxBatchSize = new RollMaxBatchSize();
    @Builder.Default public int delayAfterStartup = 0;
  }

  @Getter
  @Setter
  public static class KubernetesUpgradeCommonParams {
    private String universeName;
    private String masterAddresses;
    private KubernetesPlacement placement;
    private String ybSoftwareVersion;
    private String universeOverrides;
    private Map<String, String> azOverrides;
    private boolean newNamingStyle;
    private boolean enableYbc;
    private String ybcSoftwareVersion;

    public KubernetesUpgradeCommonParams(
        Universe universe, Cluster cluster, RuntimeConfGetter confGetter) {
      UniverseDefinitionTaskParams universeParams = universe.getUniverseDetails();
      Cluster primaryCluster = universeParams.getPrimaryCluster();
      KubernetesPlacement primaryClusterPlacement =
          new KubernetesPlacement(primaryCluster.placementInfo, false /* isReadOnlyCluster */);
      Provider provider =
          Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));

      this.universeName = universe.getName();
      this.newNamingStyle = universeParams.useNewHelmNamingStyle;
      this.masterAddresses =
          KubernetesUtil.computeMasterAddresses(
              primaryCluster.placementInfo,
              primaryClusterPlacement.masters,
              universeParams.nodePrefix,
              this.universeName,
              provider,
              universeParams.communicationPorts.masterRpcPort,
              this.newNamingStyle);
      this.ybSoftwareVersion = primaryCluster.userIntent.ybSoftwareVersion;
      // Overrides are always taken from the primary cluster
      this.universeOverrides = primaryCluster.userIntent.universeOverrides;
      this.azOverrides = primaryCluster.userIntent.azOverrides;
      if (this.azOverrides == null) {
        this.azOverrides = new HashMap<String, String>();
      }
      this.placement =
          primaryCluster.uuid != cluster.uuid
              ? new KubernetesPlacement(cluster.placementInfo, true /* isReadOnlyCluster */)
              : primaryClusterPlacement;
      this.enableYbc = universe.isYbcEnabled();
      this.ybcSoftwareVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);
    }
  }

  public void createPodsTask(
      String universeName,
      KubernetesPlacement placement,
      String masterAddresses,
      boolean isReadOnlyCluster,
      boolean enableYbc) {
    createPodsTask(
        universeName,
        placement,
        masterAddresses,
        null,
        null,
        new PlacementInfo(),
        isReadOnlyCluster,
        enableYbc);
  }

  public void createPodsTask(
      String universeName,
      KubernetesPlacement newPlacement,
      String masterAddresses,
      KubernetesPlacement currPlacement,
      ServerType serverType,
      PlacementInfo activeZones,
      boolean isReadOnlyCluster,
      boolean enableYbc) {
    createPodsTask(
        universeName,
        newPlacement,
        masterAddresses,
        currPlacement,
        serverType,
        activeZones,
        isReadOnlyCluster,
        enableYbc,
        false /* usePreviousGflagsChecksum */);
  }

  public void createPauseKubernetesUniverseTasks(String universeName) {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }

    Provider provider =
        Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

    SubTaskGroup pauseGroup = createSubTaskGroup("Pause Kubernetes Universe");
    pauseGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);

    KubernetesPlacement placement = new KubernetesPlacement(primaryCluster.placementInfo, false);

    for (Entry<UUID, Map<String, String>> entry : placement.configs.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;
      Map<String, String> config = entry.getValue();

      KubernetesCommandExecutor pauseTask =
          createPauseKubernetesUniverseTaskAZ(
              universeName,
              taskParams().nodePrefix,
              azCode,
              config,
              placement.masters.getOrDefault(azUUID, 0),
              false, // isReadOnlyCluster
              taskParams().useNewHelmNamingStyle,
              taskParams().getUniverseUUID());

      pauseGroup.addSubTask(pauseTask);
    }

    if (universe.getUniverseDetails().getReadOnlyClusters().size() > 0) {
      log.info("Creating pause Kubernetes universe tasks for read only cluster");
      Cluster readOnlyCluster = universe.getUniverseDetails().getReadOnlyClusters().get(0);
      KubernetesPlacement readOnlyPlacement =
          new KubernetesPlacement(readOnlyCluster.placementInfo, true);
      for (Entry<UUID, Map<String, String>> entry : readOnlyPlacement.configs.entrySet()) {
        UUID azUUID = entry.getKey();
        String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;
        Map<String, String> config = entry.getValue();
        KubernetesCommandExecutor pauseReadonlyTask =
            createPauseKubernetesUniverseTaskAZ(
                universeName,
                taskParams().nodePrefix,
                azCode,
                config,
                0, // No masters in read only cluster
                true, // isReadOnlyCluster
                taskParams().useNewHelmNamingStyle,
                taskParams().getUniverseUUID());
        pauseGroup.addSubTask(pauseReadonlyTask);
      }
    }

    getRunnableTask().addSubTaskGroup(pauseGroup);
  }

  public void createResumeKubernetesUniverseTasks() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String universeName = universe.getName();
    log.info("Creating resume Kubernetes universe tasks for universe {}", universeName);
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }

    Provider provider =
        Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    UUID providerUUID = provider.getUuid();

    SubTaskGroup resumeGroup = createSubTaskGroup("Resume Kubernetes Universe");
    resumeGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);

    KubernetesPlacement placement = new KubernetesPlacement(primaryCluster.placementInfo, false);

    for (Entry<UUID, Map<String, String>> entry : placement.configs.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;
      Map<String, String> config = entry.getValue();

      KubernetesCommandExecutor resumeTask =
          createResumeKubernetesUniverseTaskAZ(
              universeName,
              taskParams().nodePrefix,
              azCode,
              config,
              placement.masters.getOrDefault(azUUID, 0),
              false, // isReadOnlyCluster
              taskParams().useNewHelmNamingStyle,
              taskParams().getUniverseUUID(),
              providerUUID);

      resumeGroup.addSubTask(resumeTask);
    }

    if (universe.getUniverseDetails().getReadOnlyClusters().size() > 0) {
      log.info("Creating resume Kubernetes universe tasks for read only cluster");
      Cluster readOnlyCluster = universe.getUniverseDetails().getReadOnlyClusters().get(0);
      providerUUID = UUID.fromString(readOnlyCluster.userIntent.provider);
      KubernetesPlacement readOnlyPlacement =
          new KubernetesPlacement(readOnlyCluster.placementInfo, true);
      for (Entry<UUID, Map<String, String>> entry : readOnlyPlacement.configs.entrySet()) {
        UUID azUUID = entry.getKey();
        String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;
        Map<String, String> config = entry.getValue();
        KubernetesCommandExecutor resumeReadonlyTask =
            createResumeKubernetesUniverseTaskAZ(
                universeName,
                taskParams().nodePrefix,
                azCode,
                config,
                0, // No masters in read only cluster
                true, // isReadOnlyCluster
                taskParams().useNewHelmNamingStyle,
                taskParams().getUniverseUUID(),
                providerUUID);
        resumeGroup.addSubTask(resumeReadonlyTask);
      }
    }
    getRunnableTask().addSubTaskGroup(resumeGroup);

    // Wait for nodes to start after resume
    List<NodeDetails> tserverNodeList = universe.getTServers();
    List<NodeDetails> masterNodeList = universe.getMasters();
    createWaitForServersTasks(tserverNodeList, ServerType.TSERVER);
    createWaitForServersTasks(masterNodeList, ServerType.MASTER);
    createWaitForYbcServerTask(tserverNodeList);

    createUnivManageAlertDefinitionsTask(true).setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);

    createSwamperTargetUpdateTask(false);

    createMarkSourceMetricsTask(universe, MetricSourceState.ACTIVE)
        .setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);

    createUpdateUniverseFieldsTask(
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.universePaused = false;
          u.setUniverseDetails(details);
        });
  }

  public void createPodsTask(
      String universeName,
      KubernetesPlacement newPlacement,
      String masterAddresses,
      KubernetesPlacement currPlacement,
      ServerType serverType,
      PlacementInfo activeZones,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      boolean usePreviousGflagsChecksum) {
    String ybSoftwareVersion;
    Cluster primaryCluster;

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (isReadOnlyCluster) {
      ybSoftwareVersion = taskParams().getReadOnlyClusters().get(0).userIntent.ybSoftwareVersion;
      primaryCluster = taskParams().getPrimaryCluster();
      if (primaryCluster == null) {
        primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
      }
      String primaryClusterVersion = primaryCluster.userIntent.ybSoftwareVersion;
      if (!primaryClusterVersion.equals(ybSoftwareVersion)) {
        String msg =
            String.format(
                "Read cluster software version %s is not matching with"
                    + " primary cluster software version %s",
                ybSoftwareVersion, primaryClusterVersion);
        throw new IllegalArgumentException(msg);
      }
    } else {
      primaryCluster = taskParams().getPrimaryCluster();
      ybSoftwareVersion = primaryCluster.userIntent.ybSoftwareVersion;
    }

    boolean edit = currPlacement != null;

    Provider provider =
        isReadOnlyCluster
            ? Provider.getOrBadRequest(
                UUID.fromString(taskParams().getReadOnlyClusters().get(0).userIntent.provider))
            : Provider.getOrBadRequest(
                UUID.fromString(taskParams().getPrimaryCluster().userIntent.provider));
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    Map<UUID, Map<String, String>> activeDeploymentConfigs =
        KubernetesUtil.getConfigPerAZ(activeZones);
    // Only used for new deployments, so maybe empty.
    SubTaskGroup createNamespaces =
        createSubTaskGroup(
            KubernetesCommandExecutor.CommandType.CREATE_NAMESPACE.getSubTaskGroupName());
    createNamespaces.setSubTaskGroupType(SubTaskGroupType.Provisioning);

    SubTaskGroup applySecrets =
        createSubTaskGroup(
            KubernetesCommandExecutor.CommandType.APPLY_SECRET.getSubTaskGroupName());
    applySecrets.setSubTaskGroupType(SubTaskGroupType.Provisioning);

    SubTaskGroup helmInstalls =
        createSubTaskGroup(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL.getSubTaskGroupName());
    helmInstalls.setSubTaskGroupType(SubTaskGroupType.Provisioning);

    SubTaskGroup podsWait =
        createSubTaskGroup(KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS.getSubTaskGroupName());
    podsWait.setSubTaskGroupType(SubTaskGroupType.Provisioning);

    Map<String, Object> universeOverrides =
        HelmUtils.convertYamlToMap(primaryCluster.userIntent.universeOverrides);
    Map<String, String> azsOverrides = primaryCluster.userIntent.azOverrides;
    if (azsOverrides == null) {
      azsOverrides = new HashMap<>();
    }

    Set<UUID> namespaceServiceOwners;
    try {
      namespaceServiceOwners =
          KubernetesUtil.getNSScopedServiceOwners(
              taskParams(),
              universe.getConfig(),
              isReadOnlyCluster ? ClusterType.ASYNC : ClusterType.PRIMARY);
    } catch (IOException e) {
      throw new RuntimeException("Parsing overrides failed!", e.getCause());
    }

    for (Entry<UUID, Map<String, String>> entry : newPlacement.configs.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;

      if (!newPlacement.masters.containsKey(azUUID) && serverType == ServerType.MASTER) {
        continue;
      }

      // If set of owners contains this azUUID, make this the namespaced scope services owner
      boolean namespacedServiceReleaseOwner = namespaceServiceOwners.contains(azUUID);

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

      String azOverridesStr =
          azsOverrides.get(PlacementInfoUtil.getAZNameFromUUID(provider, azUUID));
      Map<String, Object> azOverrides = HelmUtils.convertYamlToMap(azOverridesStr);

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
                universeName,
                CommandType.HELM_UPGRADE,
                tempPI,
                azCode,
                masterAddresses,
                ybSoftwareVersion,
                serverType,
                config,
                masterPartition,
                tserverPartition,
                universeOverrides,
                azOverrides,
                isReadOnlyCluster,
                enableYbc,
                usePreviousGflagsChecksum));

        // When adding masters, the number of tservers will be still the same as before.
        // They get added later.
        int podsToWaitFor =
            serverType == ServerType.MASTER
                ? newNumMasters + currNumTservers
                : newNumMasters + newNumTservers;
        podsWait.addSubTask(
            createKubernetesCheckPodNumTask(
                universeName,
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
                  universeName,
                  KubernetesCommandExecutor.CommandType.CREATE_NAMESPACE,
                  azCode,
                  config,
                  isReadOnlyCluster));
        }

        // Apply the necessary pull secret to each namespace.
        applySecrets.addSubTask(
            createKubernetesExecutorTask(
                universeName,
                KubernetesCommandExecutor.CommandType.APPLY_SECRET,
                azCode,
                config,
                isReadOnlyCluster));

        // Create the helm deployments.
        helmInstalls.addSubTask(
            createKubernetesExecutorTask(
                universeName,
                KubernetesCommandExecutor.CommandType.HELM_INSTALL,
                tempPI,
                azCode,
                masterAddresses,
                ybSoftwareVersion,
                config,
                universeOverrides,
                azOverrides,
                isReadOnlyCluster,
                enableYbc,
                false /* usePreviousGflagsChecksum */,
                namespacedServiceReleaseOwner));

        // Add zone to active configs.
        PlacementInfoUtil.addPlacementZone(azUUID, activeZones);
      }
    }

    getRunnableTask().addSubTaskGroup(createNamespaces);
    getRunnableTask().addSubTaskGroup(applySecrets);
    getRunnableTask().addSubTaskGroup(helmInstalls);
    getRunnableTask().addSubTaskGroup(podsWait);
  }

  /*
  Performs the updates to the helm charts to modify the master addresses as well as
  update the instance type.
  */
  public void upgradePodsTask(
      String universeName,
      KubernetesPlacement newPlacement,
      String masterAddresses,
      KubernetesPlacement currPlacement,
      ServerType serverType,
      String softwareVersion,
      int waitTime,
      String universeOverridesStr,
      Map<String, String> azsOverrides,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    upgradePodsTask(
        universeName,
        newPlacement,
        masterAddresses,
        currPlacement,
        serverType,
        softwareVersion,
        waitTime,
        universeOverridesStr,
        azsOverrides,
        newNamingStyle,
        isReadOnlyCluster,
        CommandType.HELM_UPGRADE);
  }

  public void upgradePodsTask(
      String universeName,
      KubernetesPlacement newPlacement,
      String masterAddresses,
      KubernetesPlacement currPlacement,
      ServerType serverType,
      String softwareVersion,
      int waitTime,
      String universeOverridesStr,
      Map<String, String> azsOverrides,
      boolean newNamingStyle,
      boolean isReadOnlyCluster,
      CommandType commandType) {
    upgradePodsTask(
        universeName,
        newPlacement,
        masterAddresses,
        currPlacement,
        serverType,
        softwareVersion,
        universeOverridesStr,
        azsOverrides,
        newNamingStyle,
        isReadOnlyCluster,
        commandType,
        false,
        null,
        PodUpgradeParams.DEFAULT,
        null /* ysqlMajorVersionUpgradeState */,
        null /* rootCAUUID */);
  }

  public void upgradePodsNonRolling(
      String universeName,
      KubernetesPlacement placement,
      String masterAddresses,
      ServerType serverType,
      String softwareVersion,
      String universeOverridesStr,
      Map<String, String> azsOverrides,
      boolean newNamingStyle,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      String ybcSoftwareVersion,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID) {
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster =
          Universe.getOrBadRequest(taskParams().getUniverseUUID())
              .getUniverseDetails()
              .getPrimaryCluster();
    }
    String providerStr =
        isReadOnlyCluster
            ? taskParams().getReadOnlyClusters().get(0).userIntent.provider
            : primaryCluster.userIntent.provider;
    Provider provider = Provider.getOrBadRequest(UUID.fromString(providerStr));
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    String nodePrefix = taskParams().nodePrefix;

    Map<UUID, ServerType> serversToUpdate = getServersToUpdateAzMap(placement, serverType);

    Map<String, Object> universeOverrides = HelmUtils.convertYamlToMap(universeOverridesStr);
    SubTaskGroup helmUpgrade =
        createSubTaskGroup(CommandType.HELM_UPGRADE.getSubTaskGroupName(), true);
    SubTaskGroup allPodsDelete =
        createSubTaskGroup(CommandType.DELETE_ALL_SERVER_TYPE_PODS.getSubTaskGroupName(), true);
    SubTaskGroup waitForPodsLive =
        createSubTaskGroup(
            KubernetesWaitForPod.CommandType.WAIT_FOR_POD.getSubTaskGroupName(), true);
    SubTaskGroup restoreUpdateStrategy =
        createSubTaskGroup(CommandType.HELM_UPGRADE.getSubTaskGroupName(), true);
    List<NodeDetails> tserverNodes = new ArrayList<>();
    List<NodeDetails> masterNodes = new ArrayList<>();
    serversToUpdate.entrySet().stream()
        .forEach(
            serverEntry -> {
              UUID azUUID = serverEntry.getKey();
              ServerType sType = serverEntry.getValue();
              PlacementInfo tempPI = new PlacementInfo();
              PlacementInfoUtil.addPlacementZone(azUUID, tempPI);
              int numTservers = placement.tservers.getOrDefault(azUUID, 0);
              int numMasters = placement.masters.getOrDefault(azUUID, 0);
              tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = numTservers;
              tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor =
                  numMasters;
              String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;
              Map<String, String> config = placement.configs.get(azUUID);
              String azOverridesStr =
                  azsOverrides.get(PlacementInfoUtil.getAZNameFromUUID(provider, azUUID));
              Map<String, Object> azOverrides = HelmUtils.convertYamlToMap(azOverridesStr);
              helmUpgrade.addSubTask(
                  getSingleNonRollingKubernetesExecutorTaskForServerTypeTask(
                      universeName,
                      CommandType.HELM_UPGRADE,
                      tempPI,
                      azCode,
                      masterAddresses,
                      softwareVersion,
                      sType,
                      config,
                      universeOverrides,
                      azOverrides,
                      isReadOnlyCluster,
                      enableYbc,
                      ysqlMajorVersionUpgradeState,
                      rootCAUUID));
              allPodsDelete.addSubTask(
                  getSingleKubernetesExecutorTaskForServerTypeTask(
                      universeName,
                      CommandType.DELETE_ALL_SERVER_TYPE_PODS,
                      null,
                      azCode,
                      masterAddresses,
                      null,
                      sType,
                      config,
                      0,
                      0,
                      null,
                      null,
                      isReadOnlyCluster,
                      null,
                      null,
                      false,
                      null,
                      false /* usePreviousGflagsChecksum */,
                      null /* previousGflagsChecksumMap */,
                      false /* usePreviousCertChecksum */,
                      null /* previousCertChecksum */,
                      false, /* useNewMasterDiskSize */
                      false /* useNewTserverDiskSize */,
                      ysqlMajorVersionUpgradeState,
                      null /* rootCAUUID */));

              if (sType.equals(ServerType.EITHER)) {
                waitForAllServerTypePodsTask(
                    tserverNodes,
                    waitForPodsLive,
                    numTservers,
                    ServerType.TSERVER,
                    azCode,
                    nodePrefix,
                    isMultiAz,
                    newNamingStyle,
                    universeName,
                    isReadOnlyCluster,
                    config);
                waitForAllServerTypePodsTask(
                    masterNodes,
                    waitForPodsLive,
                    numMasters,
                    ServerType.MASTER,
                    azCode,
                    nodePrefix,
                    isMultiAz,
                    newNamingStyle,
                    universeName,
                    isReadOnlyCluster,
                    config);
              } else if (sType.equals(ServerType.MASTER)) {
                waitForAllServerTypePodsTask(
                    masterNodes,
                    waitForPodsLive,
                    numMasters,
                    ServerType.MASTER,
                    azCode,
                    nodePrefix,
                    isMultiAz,
                    newNamingStyle,
                    universeName,
                    isReadOnlyCluster,
                    config);
              } else if (sType.equals(ServerType.TSERVER)) {
                waitForAllServerTypePodsTask(
                    tserverNodes,
                    waitForPodsLive,
                    numTservers,
                    ServerType.TSERVER,
                    azCode,
                    nodePrefix,
                    isMultiAz,
                    newNamingStyle,
                    universeName,
                    isReadOnlyCluster,
                    config);
              }
              // When changing strategy back to Rolling, we need to keep partition for
              // server which was "not" upgraded to num_pods, as keeping partition to 0
              // will lead to unexpected restarts for server types for which we don't need
              // restarts. For eg. Setting Immutable YBC after non-restart gflags upgrade on
              // master.
              int masterPartition =
                  Arrays.asList(
                              UniverseTaskBase.ServerType.MASTER,
                              UniverseTaskBase.ServerType.EITHER)
                          .contains(sType)
                      ? 0
                      : numMasters;
              int tserverPartition =
                  Arrays.asList(
                              UniverseTaskBase.ServerType.EITHER,
                              UniverseTaskBase.ServerType.TSERVER)
                          .contains(sType)
                      ? 0
                      : numTservers;
              restoreUpdateStrategy.addSubTask(
                  getSingleKubernetesExecutorTaskForServerTypeTask(
                      universeName,
                      CommandType.HELM_UPGRADE,
                      tempPI,
                      azCode,
                      masterAddresses,
                      softwareVersion,
                      sType,
                      config,
                      masterPartition,
                      tserverPartition,
                      universeOverrides,
                      azOverrides,
                      isReadOnlyCluster,
                      null,
                      null,
                      enableYbc,
                      null,
                      false /* usePreviousGflagsChecksum */,
                      null /* previousGflagsChecksumMap */,
                      false /* usePreviousCertChecksum */,
                      null /* previousCertChecksum */,
                      false, /* useNewMasterDiskSize */
                      false /* useNewTserverDiskSize */,
                      ysqlMajorVersionUpgradeState,
                      null /* rootCAUUID */));
            });
    getRunnableTask().addSubTaskGroup(helmUpgrade);
    getRunnableTask().addSubTaskGroup(allPodsDelete);
    getRunnableTask().addSubTaskGroup(waitForPodsLive);
    getRunnableTask().addSubTaskGroup(restoreUpdateStrategy);
    createTransferXClusterCertsCopyTasks(
        Stream.concat(tserverNodes.stream(), masterNodes.stream()).collect(Collectors.toList()),
        getUniverse(),
        SubTaskGroupType.ConfigureUniverse);
    if (serverType.equals(ServerType.EITHER)) {
      createWaitForServersTasks(masterNodes, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      SubTaskGroup setEncKeys =
          createSetActiveUniverseKeysTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().addSubTaskGroup(setEncKeys);
      createWaitForServersTasks(tserverNodes, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    } else if (serverType.equals(ServerType.TSERVER)) {
      createWaitForServersTasks(tserverNodes, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    } else if (serverType.equals(ServerType.MASTER)) {
      createWaitForServersTasks(masterNodes, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      SubTaskGroup setEncKeys =
          createSetActiveUniverseKeysTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().addSubTaskGroup(setEncKeys);
    }
  }

  public void upgradePodsNonRestart(
      String universeName,
      KubernetesPlacement placement,
      String masterAddresses,
      ServerType serverType,
      String softwareVersion,
      String universeOverridesStr,
      Map<String, String> azsOverrides,
      boolean newNamingStyle,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      String ybcSoftwareVersion) {
    upgradePodsNonRestart(
        universeName,
        placement,
        masterAddresses,
        serverType,
        softwareVersion,
        universeOverridesStr,
        azsOverrides,
        newNamingStyle,
        isReadOnlyCluster,
        enableYbc,
        ybcSoftwareVersion,
        null /* ysqlMajorVersionUpgradeState */);
  }

  public void upgradePodsNonRestart(
      String universeName,
      KubernetesPlacement placement,
      String masterAddresses,
      ServerType serverType,
      String softwareVersion,
      String universeOverridesStr,
      Map<String, String> azsOverrides,
      boolean newNamingStyle,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      String ybcSoftwareVersion,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    upgradePodsNonRestart(
        universeName,
        placement,
        masterAddresses,
        serverType,
        softwareVersion,
        universeOverridesStr,
        azsOverrides,
        newNamingStyle,
        isReadOnlyCluster,
        enableYbc,
        ybcSoftwareVersion,
        ysqlMajorVersionUpgradeState,
        null /* rootCAUUID */);
  }

  public void upgradePodsNonRestart(
      String universeName,
      KubernetesPlacement placement,
      String masterAddresses,
      ServerType serverType,
      String softwareVersion,
      String universeOverridesStr,
      Map<String, String> azsOverrides,
      boolean newNamingStyle,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      String ybcSoftwareVersion,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID) {
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster =
          Universe.getOrBadRequest(taskParams().getUniverseUUID())
              .getUniverseDetails()
              .getPrimaryCluster();
    }
    String providerStr =
        isReadOnlyCluster
            ? taskParams().getReadOnlyClusters().get(0).userIntent.provider
            : primaryCluster.userIntent.provider;
    Provider provider = Provider.getOrBadRequest(UUID.fromString(providerStr));
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

    SubTaskGroup helmUpgrade = createSubTaskGroup(CommandType.HELM_UPGRADE.getSubTaskGroupName());
    helmUpgrade.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    Map<UUID, ServerType> serversToUpdate = getServersToUpdateAzMap(placement, serverType);
    Map<String, Object> universeOverrides = HelmUtils.convertYamlToMap(universeOverridesStr);
    serversToUpdate.entrySet().stream()
        .forEach(
            serverEntry -> {
              UUID azUUID = serverEntry.getKey();
              ServerType sType = serverEntry.getValue();
              PlacementInfo tempPI = new PlacementInfo();
              PlacementInfoUtil.addPlacementZone(azUUID, tempPI);
              int numTservers = placement.tservers.getOrDefault(azUUID, 0);
              int numMasters = placement.masters.getOrDefault(azUUID, 0);
              tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = numTservers;
              tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor =
                  numMasters;
              String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;
              Map<String, String> config = placement.configs.get(azUUID);
              String azOverridesStr =
                  azsOverrides.get(PlacementInfoUtil.getAZNameFromUUID(provider, azUUID));
              Map<String, Object> azOverrides = HelmUtils.convertYamlToMap(azOverridesStr);
              // Master and Tserver partition are equal to num_pods for non-restart helm upgrade.
              // They will be reset on next rolling upgrade for both server types.
              // This is to prevent unexpected restarts due to diverged values.
              helmUpgrade.addSubTask(
                  getSingleNonRestartKubernetesExecutorTaskForServerTypeTask(
                      universeName,
                      tempPI,
                      azCode,
                      masterAddresses,
                      softwareVersion,
                      sType,
                      numMasters /* masterPartition */,
                      numTservers /* tserverPartition */,
                      config,
                      universeOverrides,
                      azOverrides,
                      isReadOnlyCluster,
                      enableYbc,
                      ysqlMajorVersionUpgradeState,
                      rootCAUUID));
            });
    getRunnableTask().addSubTaskGroup(helmUpgrade);
    // Wait for gflags change to be reflected on mounted locations
    createWaitForDurationSubtask(
            taskParams().getUniverseUUID(),
            Duration.ofSeconds(confGetter.getGlobalConf(GlobalConfKeys.waitForK8sGFlagSyncSec)))
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  private Map<UUID, ServerType> getServersToUpdateAzMap(
      KubernetesPlacement placement, ServerType serverType) {
    Map<UUID, ServerType> serversToUpdate = new HashMap<>();
    if (serverType.equals(ServerType.EITHER)) {
      placement.masters.keySet().stream()
          .forEach(azUUID -> serversToUpdate.put(azUUID, ServerType.MASTER));
      placement.tservers.keySet().stream()
          .forEach(
              azUUID -> {
                if (serversToUpdate.containsKey(azUUID)) {
                  serversToUpdate.put(azUUID, ServerType.EITHER);
                } else {
                  serversToUpdate.put(azUUID, ServerType.TSERVER);
                }
              });
    } else if (serverType.equals(ServerType.MASTER)) {
      placement.masters.keySet().stream()
          .forEach(azUUID -> serversToUpdate.put(azUUID, ServerType.MASTER));
    } else {
      placement.tservers.keySet().stream()
          .forEach(azUUID -> serversToUpdate.put(azUUID, ServerType.TSERVER));
    }
    return serversToUpdate;
  }

  private void waitForAllServerTypePodsTask(
      List<NodeDetails> nodes,
      SubTaskGroup waitForPodsLive,
      int numPods,
      ServerType serverType,
      String azCode,
      String nodePrefix,
      boolean isMultiAz,
      boolean newNamingStyle,
      String universeName,
      boolean isReadOnlyCluster,
      Map<String, String> config) {
    for (int podIndex = 0; podIndex <= numPods - 1; podIndex++) {
      String podName =
          KubernetesUtil.getPodName(
              podIndex,
              azCode,
              serverType,
              nodePrefix,
              isMultiAz,
              newNamingStyle,
              universeName,
              isReadOnlyCluster);
      NodeDetails node =
          KubernetesUtil.getKubernetesNodeName(
              podIndex, azCode, serverType, isMultiAz, isReadOnlyCluster);
      waitForPodsLive.addSubTask(
          getKubernetesWaitForPodTask(
              universeName,
              KubernetesWaitForPod.CommandType.WAIT_FOR_POD,
              podName,
              azCode,
              config,
              isReadOnlyCluster));
      nodes.add(node);
    }
  }

  public void upgradePodsTask(
      String universeName,
      KubernetesPlacement newPlacement,
      String masterAddresses,
      KubernetesPlacement currPlacement,
      ServerType serverType,
      String softwareVersion,
      String universeOverridesStr,
      Map<String, String> azsOverrides,
      boolean newNamingStyle,
      boolean isReadOnlyCluster,
      CommandType commandType,
      boolean enableYbc,
      String ybcSoftwareVersion,
      PodUpgradeParams podUpgradeParams,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID) {
    upgradePodsTask(
        universeName,
        newPlacement,
        masterAddresses,
        currPlacement,
        serverType,
        softwareVersion,
        universeOverridesStr,
        azsOverrides,
        newNamingStyle,
        isReadOnlyCluster,
        commandType,
        enableYbc,
        ybcSoftwareVersion,
        podUpgradeParams,
        ysqlMajorVersionUpgradeState,
        rootCAUUID,
        false /* useExistingServerCert */);
  }

  public void upgradePodsTask(
      String universeName,
      KubernetesPlacement newPlacement,
      String masterAddresses,
      KubernetesPlacement currPlacement,
      ServerType serverType,
      String softwareVersion,
      String universeOverridesStr,
      Map<String, String> azsOverrides,
      boolean newNamingStyle,
      boolean isReadOnlyCluster,
      CommandType commandType,
      boolean enableYbc,
      String ybcSoftwareVersion,
      PodUpgradeParams podUpgradeParams,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID,
      boolean useExistingServerCert) {
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster =
          Universe.getOrBadRequest(taskParams().getUniverseUUID())
              .getUniverseDetails()
              .getPrimaryCluster();
    }
    boolean edit = currPlacement != null;
    String providerStr =
        isReadOnlyCluster
            ? taskParams().getReadOnlyClusters().get(0).userIntent.provider
            : primaryCluster.userIntent.provider;
    Provider provider = Provider.getOrBadRequest(UUID.fromString(providerStr));
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    String nodePrefix = taskParams().nodePrefix;

    Map<UUID, Integer> serversToUpdate =
        serverType == ServerType.MASTER ? newPlacement.masters : newPlacement.tservers;

    if (serverType == ServerType.TSERVER) {
      Map<UUID, PlacementInfo.PlacementAZ> placementAZMap =
          PlacementInfoUtil.getPlacementAZMap(newPlacement.placementInfo);

      List<UUID> sortedZonesToUpdate =
          serversToUpdate.keySet().stream()
              .sorted(Comparator.comparing(zoneUUID -> !placementAZMap.get(zoneUUID).isAffinitized))
              .collect(Collectors.toList());

      // Put isAffinitized availability zones first
      serversToUpdate =
          sortedZonesToUpdate.stream()
              .collect(
                  Collectors.toMap(
                      Function.identity(), serversToUpdate::get, (a, b) -> a, LinkedHashMap::new));
    }

    if (serverType == ServerType.TSERVER && !edit) {
      // clear blacklist
      clearLeaderBlacklistIfAvailable(SubTaskGroupType.ConfigureUniverse);
    }

    Map<String, Object> universeOverrides = HelmUtils.convertYamlToMap(universeOverridesStr);
    for (Entry<UUID, Integer> entry : serversToUpdate.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;

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
          numPods = Math.min(newNumTservers, currNumTservers);
          if (currNumTservers == 0) {
            continue;
          }
        }
      }

      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = newNumTservers;
      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor = newNumMasters;

      Map<String, String> config = newPlacement.configs.get(azUUID);
      String azOverridesStr =
          azsOverrides.get(PlacementInfoUtil.getAZNameFromUUID(provider, azUUID));
      Map<String, Object> azOverrides = HelmUtils.convertYamlToMap(azOverridesStr);
      Set<ServerType> serverTypes = Collections.singleton(serverType);

      for (KubernetesPartitions.KubernetesPartition partition :
          KubernetesPartitions.iterable(
              numPods,
              serverType,
              isReadOnlyCluster,
              edit ? currNumMasters : newNumMasters,
              edit ? currNumTservers : newNumTservers,
              podUpgradeParams,
              (part) ->
                  KubernetesUtil.getPodName(
                      part,
                      azCode,
                      serverType,
                      nodePrefix,
                      isMultiAz,
                      newNamingStyle,
                      universeName,
                      isReadOnlyCluster),
              (part) ->
                  KubernetesUtil.getKubernetesNodeName(
                      part, azCode, serverType, isMultiAz, isReadOnlyCluster))) {

        final List<NodeDetails> nodeList = partition.nodeList;
        final List<String> podNames = partition.podNames;
        for (NodeDetails node : nodeList) {
          if (!isSkipPrechecks()) {
            createNodePrecheckTasks(
                node, serverTypes, SubTaskGroupType.ConfigureUniverse, true, softwareVersion);
          }
        }
        if (!isReadOnlyCluster && !isSkipPrechecks()) {
          createCheckNodesAreSafeToTakeDownTask(
              Collections.singletonList(
                  UpgradeTaskBase.MastersAndTservers.from(nodeList, serverTypes)),
              softwareVersion,
              false);
        }
        if (serverType == ServerType.TSERVER && !edit) {
          addLeaderBlackListIfAvailable(nodeList, SubTaskGroupType.ConfigureUniverse);
        }
        if (commandType == CommandType.POD_DELETE) {
          addParallelTasks(
              podNames,
              podName ->
                  getSingleKubernetesExecutorTaskForServerTypeTask(
                      universeName,
                      commandType,
                      tempPI,
                      azCode,
                      masterAddresses,
                      softwareVersion,
                      serverType,
                      config,
                      0, // Don't need partitions for pod delete.
                      0, // Don't need partitions for pod delete.
                      universeOverrides,
                      azOverrides,
                      isReadOnlyCluster,
                      podName,
                      null,
                      enableYbc,
                      ybcSoftwareVersion,
                      false /* usePreviousGflagsChecksum */,
                      null /* previousGflagsChecksumMap */,
                      false /* usePreviousCertChecksum */,
                      null /* previousCertChecksum */,
                      false /* useNewMasterDiskSize */,
                      false /* useNewTserverDiskSize */,
                      ysqlMajorVersionUpgradeState,
                      null /* rootCAUUID */),
              commandType.getSubTaskGroupName(),
              UserTaskDetails.SubTaskGroupType.Provisioning,
              false);
        } else {
          createSingleKubernetesExecutorTaskForServerType(
              universeName,
              commandType,
              tempPI,
              azCode,
              masterAddresses,
              softwareVersion,
              serverType,
              config,
              partition.masterPartition,
              partition.tserverPartition,
              universeOverrides,
              azOverrides,
              isReadOnlyCluster,
              podNames.get(0),
              null,
              false,
              enableYbc,
              ybcSoftwareVersion,
              false /* usePreviousGflagsChecksum */,
              null /* previousGflagsChecksumMap */,
              ysqlMajorVersionUpgradeState,
              rootCAUUID,
              useExistingServerCert);
        }

        addParallelTasks(
            podNames,
            podName ->
                getKubernetesWaitForPodTask(
                    universeName,
                    KubernetesWaitForPod.CommandType.WAIT_FOR_POD,
                    podName,
                    azCode,
                    config,
                    isReadOnlyCluster),
            commandType.getSubTaskGroupName(),
            UserTaskDetails.SubTaskGroupType.Provisioning);

        // Copy the source root certificate to the pods.
        createTransferXClusterCertsCopyTasks(
            nodeList, getUniverse(), SubTaskGroupType.ConfigureUniverse);

        createWaitForServersTasks(nodeList, serverType)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        addParallelTasks(
            nodeList,
            node -> getWaitForServerReadyTask(node, serverType),
            "WaitForServerReady",
            UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        if (podUpgradeParams.delayAfterStartup > 0) {
          createSleepAfterStartupTask(
              taskParams().getUniverseUUID(),
              Collections.singletonList(serverType),
              KubernetesCommandExecutor.getPodCommandDateKey(podNames.get(0), commandType),
              podUpgradeParams.delayAfterStartup);
        }

        // If there are no universe keys on the universe, it will have no effect.
        if (serverType == ServerType.MASTER
            && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
          createSetActiveUniverseKeysTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        }

        if (serverType == ServerType.TSERVER && !edit) {
          removeFromLeaderBlackListIfAvailable(nodeList, SubTaskGroupType.ConfigureUniverse);
        }
        // Create post upgrade subtasks
        createPostUpgradeChecks(serverType, nodeList);
      }
    }
  }

  public void deletePodsTask(
      String universeName,
      KubernetesPlacement currPlacement,
      String masterAddresses,
      KubernetesPlacement newPlacement,
      boolean instanceTypeChanged,
      boolean isMultiAz,
      Provider provider,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      boolean enableYbc) {
    deletePodsTask(
        universeName,
        currPlacement,
        masterAddresses,
        newPlacement,
        instanceTypeChanged,
        isMultiAz,
        provider,
        isReadOnlyCluster,
        newNamingStyle,
        enableYbc,
        false /* usePreviousGflagsChecksum */);
  }

  public void deletePodsTask(
      String universeName,
      KubernetesPlacement currPlacement,
      String masterAddresses,
      KubernetesPlacement newPlacement,
      boolean instanceTypeChanged,
      boolean isMultiAz,
      Provider provider,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      boolean enableYbc,
      boolean usePreviousGflagsChecksum) {
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }
    String ybSoftwareVersion = primaryCluster.userIntent.ybSoftwareVersion;

    boolean edit = newPlacement != null;

    // If no config in new placement, delete deployment.
    SubTaskGroup helmDeletes =
        createSubTaskGroup(KubernetesCommandExecutor.CommandType.HELM_DELETE.getSubTaskGroupName());
    helmDeletes.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

    SubTaskGroup volumeDeletes =
        createSubTaskGroup(
            KubernetesCommandExecutor.CommandType.VOLUME_DELETE.getSubTaskGroupName());
    volumeDeletes.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

    SubTaskGroup namespaceDeletes =
        createSubTaskGroup(
            KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE.getSubTaskGroupName());
    namespaceDeletes.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
    // In case we are keeping deployment and reducing the number of masters, we need to remove the
    // PVCs for masters.
    SubTaskGroup pvcDeletes =
        createSubTaskGroup(
            KubernetesCommandExecutor.CommandType.VOLUME_DELETE_SHELL_MODE_MASTER
                .getSubTaskGroupName());
    pvcDeletes.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
    SubTaskGroup podsWait =
        createSubTaskGroup(KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS.getSubTaskGroupName());
    podsWait.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

    Map<String, Object> universeOverrides =
        HelmUtils.convertYamlToMap(primaryCluster.userIntent.universeOverrides);
    Map<String, String> azsOverrides = primaryCluster.userIntent.azOverrides;
    if (azsOverrides == null) {
      azsOverrides = new HashMap<>();
    }

    for (Entry<UUID, Map<String, String>> entry : currPlacement.configs.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;
      Map<String, String> config = entry.getValue();

      // If the new placement also has the AZ, we need to scale down. But if there
      // was a change in the instance type, the updateRemainingPod itself would have taken care
      // of the deployments' scale down.
      boolean keepDeployment = false;
      if (edit) {
        keepDeployment = newPlacement.configs.containsKey(azUUID);
        if (keepDeployment && instanceTypeChanged) {
          continue;
        }
      }

      if (keepDeployment) {
        PlacementInfo tempPI = new PlacementInfo();
        PlacementInfoUtil.addPlacementZone(azUUID, tempPI);
        tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ =
            newPlacement.tservers.get(azUUID);
        tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor =
            newPlacement.masters.getOrDefault(azUUID, 0);

        String azOverridesStr =
            azsOverrides.get(PlacementInfoUtil.getAZNameFromUUID(provider, azUUID));
        Map<String, Object> azOverrides = HelmUtils.convertYamlToMap(azOverridesStr);

        helmDeletes.addSubTask(
            createKubernetesExecutorTask(
                universeName,
                CommandType.HELM_UPGRADE,
                tempPI,
                azCode,
                masterAddresses,
                ybSoftwareVersion,
                config,
                universeOverrides,
                azOverrides,
                isReadOnlyCluster,
                enableYbc,
                usePreviousGflagsChecksum,
                false /* namespacedServiceReleaseOwner */));

        podsWait.addSubTask(
            createKubernetesCheckPodNumTask(
                universeName,
                KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS,
                azCode,
                config,
                newPlacement.tservers.get(azUUID) + newPlacement.masters.getOrDefault(azUUID, 0),
                isReadOnlyCluster));
        // New Masters are up, garbage colllect the extra master volumes now.
        pvcDeletes.addSubTask(
            garbageCollectMasterVolumes(
                universeName,
                taskParams().nodePrefix,
                azCode,
                config,
                newPlacement.masters.getOrDefault(azUUID, 0),
                isReadOnlyCluster,
                taskParams().useNewHelmNamingStyle,
                taskParams().getUniverseUUID()));
      } else {
        // Delete the helm deployments.
        helmDeletes.addSubTask(
            createKubernetesExecutorTask(
                universeName,
                KubernetesCommandExecutor.CommandType.HELM_DELETE,
                azCode,
                config,
                isReadOnlyCluster));

        // Delete the PVs created for the deployments.
        volumeDeletes.addSubTask(
            createKubernetesExecutorTask(
                universeName,
                KubernetesCommandExecutor.CommandType.VOLUME_DELETE,
                azCode,
                config,
                isReadOnlyCluster));

        // If the namespace is configured at the AZ, we don't delete
        // it, as it is not created by us.
        // In case of new naming style other AZs also run in the same namespace so skip deleting
        // namespace.
        if (config.get("KUBENAMESPACE") == null && !newNamingStyle) {
          // Delete the namespaces of the deployments.
          namespaceDeletes.addSubTask(
              createKubernetesExecutorTask(
                  universeName,
                  KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE,
                  azCode,
                  config,
                  isReadOnlyCluster));
        }
      }
    }
    getRunnableTask().addSubTaskGroup(helmDeletes);
    getRunnableTask().addSubTaskGroup(volumeDeletes);
    getRunnableTask().addSubTaskGroup(namespaceDeletes);
    getRunnableTask().addSubTaskGroup(podsWait);
    getRunnableTask().addSubTaskGroup(pvcDeletes);
  }

  /*
  Sends a collection of all the pods that need to be added.
  */
  public static Set<NodeDetails> getPodsToAdd(
      Map<UUID, Integer> newPlacement,
      Map<UUID, Integer> currPlacement,
      ServerType serverType,
      boolean isMultiAz,
      boolean isReadCluster) {

    Set<NodeDetails> podsToAdd = new HashSet<>();
    for (Entry<UUID, Integer> entry : newPlacement.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = AvailabilityZone.get(azUUID).getCode();
      int numNewReplicas = entry.getValue();
      int numCurrReplicas = 0;
      if (currPlacement != null) {
        numCurrReplicas = currPlacement.getOrDefault(azUUID, 0);
      }
      for (int i = numCurrReplicas; i < numNewReplicas; i++) {
        NodeDetails node =
            KubernetesUtil.getKubernetesNodeName(i, azCode, serverType, isMultiAz, isReadCluster);
        podsToAdd.add(node);
      }
    }
    return podsToAdd;
  }

  /*
  Sends a collection of all the pods that need to be removed.
  */
  public static Set<NodeDetails> getPodsToRemove(
      Map<UUID, Integer> newPlacement,
      Map<UUID, Integer> currPlacement,
      ServerType serverType,
      Universe universe,
      boolean isMultiAz,
      boolean isReadCluster) {
    Set<NodeDetails> podsToRemove = new HashSet<>();
    for (Entry<UUID, Integer> entry : currPlacement.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = AvailabilityZone.get(azUUID).getCode();
      int numCurrReplicas = entry.getValue();
      int numNewReplicas = newPlacement.getOrDefault(azUUID, 0);
      for (int i = numCurrReplicas - 1; i >= numNewReplicas; i--) {
        NodeDetails node =
            KubernetesUtil.getKubernetesNodeName(i, azCode, serverType, isMultiAz, isReadCluster);
        NodeDetails universeNode = universe.getNode(node.nodeName);
        // This node can be null if we are coming here on a retry.
        // This means node has already been removed from the universe so we will not find in
        // podInfos that we have collected the last time round.
        if (universeNode != null) {
          podsToRemove.add(universeNode);
        }
      }
    }
    return podsToRemove;
  }

  // Create Kubernetes Executor task for creating the namespaces and pull secrets.
  public KubernetesCommandExecutor createKubernetesExecutorTask(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      String az,
      Map<String, String> config,
      boolean isReadOnlyCluster) {
    return createKubernetesExecutorTask(
        universeName, commandType, null, az, null, null, config, null, null, isReadOnlyCluster);
  }

  public KubernetesCommandExecutor createKubernetesExecutorTask(
      String universeName,
      CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      Map<String, String> config,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster) {
    return createKubernetesExecutorTask(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        config,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        false /* enableYbc */);
  }

  public KubernetesCommandExecutor createKubernetesExecutorTask(
      String universeName,
      CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      Map<String, String> config,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      boolean enableYbc) {
    return createKubernetesExecutorTask(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        config,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        enableYbc,
        false /* usePreviousGflagsChecksum */,
        false /* namespacedServiceReleaseOwner */);
  }

  // Create the Kubernetes Executor task for the helm deployments. (USED)
  public KubernetesCommandExecutor createKubernetesExecutorTask(
      String universeName,
      CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      Map<String, String> config,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      boolean usePreviousGflagsChecksum,
      boolean namespacedServiceReleaseOwner) {
    return createKubernetesExecutorTaskForServerType(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        ServerType.EITHER,
        config,
        0 /* master partition */,
        0 /* tserver partition */,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        enableYbc,
        usePreviousGflagsChecksum,
        namespacedServiceReleaseOwner);
  }

  public void installYbcOnThePods(
      Set<NodeDetails> servers,
      boolean isReadOnlyCluster,
      String ybcSoftwareVersion,
      Map<String, String> ybcGflags) {
    installYbcOnThePods(
        servers, isReadOnlyCluster, ybcSoftwareVersion, ybcGflags, null /* placement */);
  }

  public void installYbcOnThePods(
      Set<NodeDetails> servers,
      boolean isReadOnlyCluster,
      String ybcSoftwareVersion,
      Map<String, String> ybcGflags,
      @Nullable KubernetesPlacement placement) {
    SubTaskGroup ybcUpload =
        createSubTaskGroup(KubernetesCommandExecutor.CommandType.COPY_PACKAGE.getSubTaskGroupName())
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    createKubernetesYbcCopyPackageTask(
        ybcUpload, servers, isReadOnlyCluster, ybcSoftwareVersion, ybcGflags, placement);
    getRunnableTask().addSubTaskGroup(ybcUpload);
  }

  public void performYbcAction(
      Set<NodeDetails> servers, boolean isReadOnlyCluster, String command) {
    performYbcAction(servers, isReadOnlyCluster, command, null /* placement */);
  }

  public void performYbcAction(
      Set<NodeDetails> servers,
      boolean isReadOnlyCluster,
      String command,
      @Nullable KubernetesPlacement placement) {
    SubTaskGroup ybcAction =
        createSubTaskGroup(KubernetesCommandExecutor.CommandType.YBC_ACTION.getSubTaskGroupName())
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
    createKubernetesYbcActionTask(ybcAction, servers, isReadOnlyCluster, command, placement);
    getRunnableTask().addSubTaskGroup(ybcAction);
  }

  // Create Kubernetes Executor task for copying YBC package and conf file to the pod
  // If this task should not be performed on a universe, eg: Universe using inbuilt
  // YBC, check for that in callsite.
  public void createKubernetesYbcCopyPackageTask(
      SubTaskGroup subTaskGroup,
      Set<NodeDetails> servers,
      boolean isReadOnlyCluster,
      String ybcSoftwareVersion,
      Map<String, String> ybcGflags,
      @Nullable KubernetesPlacement placement) {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    for (NodeDetails node : servers) {
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      if (primaryCluster == null) {
        primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
      }
      List<Cluster> readOnlyClusters = taskParams().getReadOnlyClusters();
      if (isReadOnlyCluster && readOnlyClusters.size() == 0) {
        readOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
      }
      UUID providerUUID =
          isReadOnlyCluster
              ? UUID.fromString(readOnlyClusters.get(0).userIntent.provider)
              : UUID.fromString(primaryCluster.userIntent.provider);
      createKubernetesYbcCopyPackageSubTask(
          subTaskGroup, node, providerUUID, ybcSoftwareVersion, ybcGflags, placement);
    }
  }

  public KubernetesCommandExecutor createPauseKubernetesUniverseTaskAZ(
      String universeName,
      String nodePrefix,
      String azCode,
      Map<String, String> config,
      int newPlacementAzMasterCount,
      boolean isReadOnlyCluster,
      boolean useNewHelmNamingStyle,
      UUID universeUUID) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.universeName = universeName;
    params.commandType = KubernetesCommandExecutor.CommandType.PAUSE_AZ;
    params.azCode = azCode;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            azCode,
            isReadOnlyCluster,
            useNewHelmNamingStyle);
    params.config = config;
    String namespace =
        KubernetesUtil.getKubernetesNamespace(
            nodePrefix, azCode, config, useNewHelmNamingStyle, isReadOnlyCluster);
    params.namespace = namespace;
    params.setUniverseUUID(universeUUID);
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  public KubernetesCommandExecutor createResumeKubernetesUniverseTaskAZ(
      String universeName,
      String nodePrefix,
      String azCode,
      Map<String, String> config,
      int newPlacementAzMasterCount,
      boolean isReadOnlyCluster,
      boolean useNewHelmNamingStyle,
      UUID universeUUID,
      UUID providerUUID) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Provider provider = Provider.getOrBadRequest(providerUUID);
    params.universeName = universeName;
    params.commandType = KubernetesCommandExecutor.CommandType.RESUME_AZ;
    params.azCode = azCode;
    params.providerUUID = providerUUID;
    params.universeDetails = taskParams();
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            azCode,
            isReadOnlyCluster,
            useNewHelmNamingStyle);
    params.config = config;
    String namespace =
        KubernetesUtil.getKubernetesNamespace(
            nodePrefix, azCode, config, useNewHelmNamingStyle, isReadOnlyCluster);
    params.namespace = namespace;
    params.setUniverseUUID(universeUUID);
    params.universeConfig = universe.getConfig();
    params.ybSoftwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  public KubernetesCommandExecutor garbageCollectMasterVolumes(
      String universeName,
      String nodePrefix,
      String azCode,
      Map<String, String> config,
      int newPlacementAzMasterCount,
      boolean isReadOnlyCluster,
      boolean useNewHelmNamingStyle,
      UUID universeUUID) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.universeName = universeName;
    params.commandType = KubernetesCommandExecutor.CommandType.VOLUME_DELETE_SHELL_MODE_MASTER;
    params.azCode = azCode;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            azCode,
            isReadOnlyCluster,
            useNewHelmNamingStyle);
    params.config = config;
    params.newPlacementAzMasterCount = newPlacementAzMasterCount;
    String namespace =
        KubernetesUtil.getKubernetesNamespace(
            nodePrefix, azCode, config, useNewHelmNamingStyle, isReadOnlyCluster);
    params.namespace = namespace;
    params.setUniverseUUID(universeUUID);
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  // Create Kubernetes Executor task for perform ybc
  public void createKubernetesYbcActionTask(
      SubTaskGroup subTaskGroup,
      Set<NodeDetails> servers,
      boolean isReadOnlyCluster,
      String command,
      @Nullable KubernetesPlacement placement) {
    for (NodeDetails node : servers) {
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      List<Cluster> readOnlyClusters = taskParams().getReadOnlyClusters();
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      if (primaryCluster == null) {
        primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
      }
      if (isReadOnlyCluster && readOnlyClusters.size() == 0) {
        readOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
      }
      UUID providerUUID =
          isReadOnlyCluster
              ? UUID.fromString(readOnlyClusters.get(0).userIntent.provider)
              : UUID.fromString(primaryCluster.userIntent.provider);
      createKubernetesYbcActionSubTask(
          subTaskGroup, node, providerUUID, isReadOnlyCluster, command, placement);
    }
  }

  public KubernetesCommandExecutor createKubernetesExecutorTaskForServerType(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      boolean usePreviousGflagsChecksum) {
    return createKubernetesExecutorTaskForServerType(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        serverType,
        config,
        masterPartition,
        tserverPartition,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        enableYbc,
        usePreviousGflagsChecksum,
        false /* namespacedServiceReleaseOwner */);
  }

  public KubernetesCommandExecutor createKubernetesExecutorTaskForServerType(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      boolean usePreviousGflagsChecksum,
      boolean namespacedServiceReleaseOwner) {
    return createKubernetesExecutorTaskForServerType(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        serverType,
        config,
        masterPartition,
        tserverPartition,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        enableYbc,
        usePreviousGflagsChecksum,
        namespacedServiceReleaseOwner,
        null /* ysqlMajorVersionUpgradeState */);
  }

  // Create and return the Kubernetes Executor task for deployment of a k8s universe.
  public KubernetesCommandExecutor createKubernetesExecutorTaskForServerType(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      boolean usePreviousGflagsChecksum,
      boolean namespacedServiceReleaseOwner,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }
    params.providerUUID =
        isReadOnlyCluster
            ? UUID.fromString(taskParams().getReadOnlyClusters().get(0).userIntent.provider)
            : UUID.fromString(primaryCluster.userIntent.provider);
    params.commandType = commandType;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.azCode = az;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            az,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle);
    params.universeOverrides = universeOverrides;
    params.azOverrides = azOverrides;

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
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    // sending in the entire taskParams only for selected commandTypes that need it
    if (commandType == CommandType.HELM_INSTALL || commandType == CommandType.HELM_UPGRADE) {
      params.universeDetails = taskParams();
      params.universeConfig = universe.getConfig();
    }
    // Case when new Universe is being created, we set the gflag "master_join_existing_cluster"
    // to 'false'.
    if ((commandType == CommandType.HELM_INSTALL) && StringUtils.isNotEmpty(masterAddresses)) {
      params.masterJoinExistingCluster = false;
    }
    if (ysqlMajorVersionUpgradeState != null) {
      params.ysqlMajorVersionUpgradeState = ysqlMajorVersionUpgradeState;
    }
    params.masterPartition = masterPartition;
    params.tserverPartition = tserverPartition;
    params.serverType = serverType;
    params.isReadOnlyCluster = isReadOnlyCluster;
    params.setEnableYbc(enableYbc);
    params.usePreviousGflagsChecksum = usePreviousGflagsChecksum;

    // Since create universe is running helm installs in parallel
    // we need to make sure only one enables namespaced service to prevent
    // race.
    if (namespacedServiceReleaseOwner && commandType == CommandType.HELM_INSTALL) {
      params.createNamespacedService = true;
    }
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    return task;
  }

  public void createSingleKubernetesExecutorTask(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      boolean isReadCluster) {
    createSingleKubernetesExecutorTask(universeName, commandType, null, isReadCluster);
  }

  // Create a single Kubernetes Executor task in case we cannot execute tasks in parallel.
  public void createSingleKubernetesExecutorTask(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      boolean isReadOnlyCluster) {
    createSingleKubernetesExecutorTaskForServerType(
        universeName,
        commandType,
        pi,
        null,
        null,
        null,
        ServerType.EITHER,
        null,
        0 /* master partition */,
        0 /* tserver partition */,
        null, /* universeOverrides */
        null, /* azOverrides */
        isReadOnlyCluster,
        null,
        null);
  }

  public void createSingleKubernetesExecutorTaskForServerType(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      String podName,
      String newDiskSize) {
    createSingleKubernetesExecutorTaskForServerType(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        serverType,
        config,
        masterPartition,
        tserverPartition,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        podName,
        newDiskSize,
        false,
        false,
        null);
  }

  // Create a single Kubernetes Executor task in case we cannot execute tasks in parallel.
  public void createSingleKubernetesExecutorTaskForServerType(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      String podName,
      String newDiskSize,
      boolean ignoreErrors) {
    createSingleKubernetesExecutorTaskForServerType(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        serverType,
        config,
        masterPartition,
        tserverPartition,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        podName,
        newDiskSize,
        ignoreErrors,
        false,
        null);
  }

  // Create a single Kubernetes Executor task in case we cannot execute tasks in parallel.
  public void createSingleKubernetesExecutorTaskForServerType(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      String podName,
      String newDiskSize,
      boolean ignoreErrors,
      boolean enableYbc,
      String ybcSoftwareVersion) {
    createSingleKubernetesExecutorTaskForServerType(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        serverType,
        config,
        masterPartition,
        tserverPartition,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        podName,
        newDiskSize,
        ignoreErrors,
        enableYbc,
        ybcSoftwareVersion,
        false /* usePreviousGflagsChecksum */,
        null /* previousGflagsChecksumMap */,
        null /* ysqlMajorVersionUpgradeState */,
        null /* rootCAUUID */);
  }

  public void createSingleKubernetesExecutorTaskForServerType(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      String podName,
      String newDiskSize,
      boolean ignoreErrors,
      boolean enableYbc,
      String ybcSoftwareVersion,
      boolean usePreviousGflagsChecksum,
      Map<ServerType, String> previousGflagsChecksumMap,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID) {
    createSingleKubernetesExecutorTaskForServerType(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        serverType,
        config,
        masterPartition,
        tserverPartition,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        podName,
        newDiskSize,
        ignoreErrors,
        enableYbc,
        ybcSoftwareVersion,
        usePreviousGflagsChecksum,
        previousGflagsChecksumMap,
        ysqlMajorVersionUpgradeState,
        rootCAUUID,
        false /* useExistingServerCert */);
  }

  // Create a single Kubernetes Executor task in case we cannot execute tasks in parallel.
  public void createSingleKubernetesExecutorTaskForServerType(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      String podName,
      String newDiskSize,
      boolean ignoreErrors,
      boolean enableYbc,
      String ybcSoftwareVersion,
      boolean usePreviousGflagsChecksum,
      Map<ServerType, String> previousGflagsChecksumMap,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID,
      boolean useExistingServerCert) {
    SubTaskGroup subTaskGroup = createSubTaskGroup(commandType.getSubTaskGroupName(), ignoreErrors);
    subTaskGroup.addSubTask(
        getSingleKubernetesExecutorTaskForServerTypeTask(
            universeName,
            commandType,
            pi,
            az,
            masterAddresses,
            ybSoftwareVersion,
            serverType,
            config,
            masterPartition,
            tserverPartition,
            universeOverrides,
            azOverrides,
            isReadOnlyCluster,
            podName,
            newDiskSize,
            enableYbc,
            ybcSoftwareVersion,
            usePreviousGflagsChecksum,
            previousGflagsChecksumMap,
            false /* usePreviousCertChecksum */,
            null /* previousCertChecksum */,
            false /* useNewMasterDiskSize */,
            false /* useNewTserverDiskSize */,
            ysqlMajorVersionUpgradeState,
            rootCAUUID,
            useExistingServerCert));
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
  }

  public KubernetesCommandExecutor getSingleKubernetesExecutorTaskForServerTypeTask(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      String podName,
      String newDiskSize,
      boolean enableYbc,
      String ybcSoftwareVersion) {
    return getSingleKubernetesExecutorTaskForServerTypeTask(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        serverType,
        config,
        masterPartition,
        tserverPartition,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        podName,
        newDiskSize,
        enableYbc,
        ybcSoftwareVersion,
        false /* usePreviousGflagsChecksum */,
        null /* previousGflagsChecksumMap */,
        false /* usePreviousCertChecksum */,
        null /* previousCertChecksum */,
        false, /* useNewMasterDiskSize */
        false /* useNewTserverDiskSize */,
        null /* ysqlMajorVersionUpgradeState */,
        null /* rootCAUUID */);
  }

  public KubernetesCommandExecutor getSingleKubernetesExecutorTaskForServerTypeTask(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      String podName,
      String newDiskSize,
      boolean enableYbc,
      String ybcSoftwareVersion,
      boolean usePreviousGflagsChecksum,
      Map<ServerType, String> previousGflagsChecksumMap,
      boolean usePreviousCertChecksum,
      String previousCertChecksum,
      boolean useNewMasterDiskSize,
      boolean useNewTserverDiskSize,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID) {
    return getSingleKubernetesExecutorTaskForServerTypeTask(
        universeName,
        commandType,
        pi,
        az,
        masterAddresses,
        ybSoftwareVersion,
        serverType,
        config,
        masterPartition,
        tserverPartition,
        universeOverrides,
        azOverrides,
        isReadOnlyCluster,
        podName,
        newDiskSize,
        enableYbc,
        ybcSoftwareVersion,
        usePreviousGflagsChecksum,
        previousGflagsChecksumMap,
        usePreviousCertChecksum,
        previousCertChecksum,
        useNewMasterDiskSize,
        useNewTserverDiskSize,
        ysqlMajorVersionUpgradeState,
        rootCAUUID,
        false /* useExistingServerCert */);
  }

  public KubernetesCommandExecutor getSingleKubernetesExecutorTaskForServerTypeTask(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      int masterPartition,
      int tserverPartition,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      String podName,
      String newDiskSize,
      boolean enableYbc,
      String ybcSoftwareVersion,
      boolean usePreviousGflagsChecksum,
      Map<ServerType, String> previousGflagsChecksumMap,
      boolean usePreviousCertChecksum,
      String previousCertChecksum,
      boolean useNewMasterDiskSize,
      boolean useNewTserverDiskSize,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID,
      boolean useExistingServerCert) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }
    params.providerUUID =
        isReadOnlyCluster
            ? UUID.fromString(taskParams().getReadOnlyClusters().get(0).userIntent.provider)
            : UUID.fromString(primaryCluster.userIntent.provider);
    params.commandType = commandType;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.azCode = az;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            az,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle);
    params.universeOverrides = universeOverrides;
    params.azOverrides = azOverrides;
    params.universeName = universeName;
    // sending in the entire taskParams only for selected commandTypes that need it
    if (commandType == CommandType.HELM_INSTALL) {
      params.universeDetails = taskParams();
      params.universeConfig = universe.getConfig();
    } else if (commandType == CommandType.HELM_UPGRADE) {
      params.universeConfig = universe.getConfig();
      if (useNewMasterDiskSize || useNewTserverDiskSize) {
        // Only update the deviceInfo all other things remain same
        params.universeDetails = universe.getUniverseDetails();
        if (useNewTserverDiskSize) {
          if (isReadOnlyCluster) {
            params.universeDetails.getReadOnlyClusters().get(0).userIntent.deviceInfo =
                taskParams().getReadOnlyClusters().get(0).userIntent.deviceInfo;
          } else {
            params.universeDetails.getPrimaryCluster().userIntent.deviceInfo =
                taskParams().getPrimaryCluster().userIntent.deviceInfo;
          }
        }
        if (useNewMasterDiskSize) {
          params.universeDetails.getPrimaryCluster().userIntent.masterDeviceInfo =
              taskParams().getPrimaryCluster().userIntent.masterDeviceInfo;
        }
      } else {
        params.universeDetails = taskParams();
      }
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
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    if (ysqlMajorVersionUpgradeState != null) {
      params.ysqlMajorVersionUpgradeState = ysqlMajorVersionUpgradeState;
    }
    params.masterPartition = masterPartition;
    params.tserverPartition = tserverPartition;
    params.serverType = serverType;
    params.isReadOnlyCluster = isReadOnlyCluster;
    params.podName = podName;
    params.newDiskSize = newDiskSize;
    params.setEnableYbc(enableYbc);
    params.setYbcSoftwareVersion(ybcSoftwareVersion);
    params.usePreviousGflagsChecksum = usePreviousGflagsChecksum;
    params.previousGflagsChecksumMap = previousGflagsChecksumMap;
    params.usePreviousCertChecksum = usePreviousCertChecksum;
    params.previousCertChecksum = previousCertChecksum;
    params.useNewMasterDiskSize = useNewMasterDiskSize;
    params.useNewTserverDiskSize = useNewTserverDiskSize;
    params.rootCA = rootCAUUID;
    params.useExistingServerCert = useExistingServerCert;
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    return task;
  }

  public KubernetesCommandExecutor getSingleNonRollingKubernetesExecutorTaskForServerTypeTask(
      String universeName,
      KubernetesCommandExecutor.CommandType commandType,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      Map<String, String> config,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }
    params.providerUUID =
        isReadOnlyCluster
            ? UUID.fromString(taskParams().getReadOnlyClusters().get(0).userIntent.provider)
            : UUID.fromString(primaryCluster.userIntent.provider);
    params.commandType = commandType;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.azCode = az;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            az,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle);
    params.universeOverrides = universeOverrides;
    params.azOverrides = azOverrides;
    params.universeName = universeName;
    params.rootCA = rootCAUUID;

    // sending in the entire taskParams only for selected commandTypes that need it
    if (commandType == CommandType.HELM_INSTALL || commandType == CommandType.HELM_UPGRADE) {
      params.universeDetails = taskParams();
      params.universeConfig = universe.getConfig();
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
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    if (ysqlMajorVersionUpgradeState != null) {
      params.ysqlMajorVersionUpgradeState = ysqlMajorVersionUpgradeState;
    }
    params.serverType = serverType;
    params.isReadOnlyCluster = isReadOnlyCluster;
    params.updateStrategy = KubernetesCommandExecutor.UpdateStrategy.OnDelete;
    params.setEnableYbc(enableYbc);
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    return task;
  }

  public KubernetesCommandExecutor getSingleNonRestartKubernetesExecutorTaskForServerTypeTask(
      String universeName,
      PlacementInfo pi,
      String az,
      String masterAddresses,
      String ybSoftwareVersion,
      ServerType serverType,
      int masterPartition,
      int tserverPartition,
      Map<String, String> config,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      boolean isReadOnlyCluster,
      boolean enableYbc,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      UUID rootCAUUID) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }
    params.providerUUID =
        isReadOnlyCluster
            ? UUID.fromString(taskParams().getReadOnlyClusters().get(0).userIntent.provider)
            : UUID.fromString(primaryCluster.userIntent.provider);
    params.commandType = CommandType.HELM_UPGRADE;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.azCode = az;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            az,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle);
    params.universeOverrides = universeOverrides;
    params.azOverrides = azOverrides;
    params.universeName = universeName;
    params.universeDetails = taskParams();
    params.universeConfig = universe.getConfig();
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
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    if (ysqlMajorVersionUpgradeState != null) {
      params.ysqlMajorVersionUpgradeState = ysqlMajorVersionUpgradeState;
    }
    params.serverType = serverType;
    params.masterPartition = masterPartition;
    params.tserverPartition = tserverPartition;
    params.isReadOnlyCluster = isReadOnlyCluster;
    params.setEnableYbc(enableYbc);
    params.usePreviousGflagsChecksum = true; /* using true always */
    params.usePreviousCertChecksum = true /* use true always */;
    params.rootCA = rootCAUUID;
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    return task;
  }

  public KubernetesWaitForPod getKubernetesWaitForPodTask(
      String universeName,
      KubernetesWaitForPod.CommandType commandType,
      String podName,
      String az,
      Map<String, String> config,
      boolean isReadOnlyCluster) {
    KubernetesWaitForPod.Params params = new KubernetesWaitForPod.Params();
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster =
          Universe.getOrBadRequest(taskParams().getUniverseUUID())
              .getUniverseDetails()
              .getPrimaryCluster();
    }
    params.providerUUID = UUID.fromString(primaryCluster.userIntent.provider);
    params.commandType = commandType;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            az,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle);
    if (config != null) {
      params.config = config;
      // This assumes that the config is az config.
      // params.namespace remains null if config is not passed.
      params.namespace =
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    params.universeUUID = taskParams().getUniverseUUID();
    params.podName = podName;
    KubernetesWaitForPod task = createTask(KubernetesWaitForPod.class);
    task.initialize(params);
    return task;
  }

  public void createKubernetesWaitForPodTask(
      String universeName,
      KubernetesWaitForPod.CommandType commandType,
      String podName,
      String az,
      Map<String, String> config,
      boolean isReadOnlyCluster) {
    SubTaskGroup subTaskGroup = createSubTaskGroup(commandType.getSubTaskGroupName());
    subTaskGroup.addSubTask(
        getKubernetesWaitForPodTask(
            universeName, commandType, podName, az, config, isReadOnlyCluster));
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.KubernetesWaitForPod);
  }

  public KubernetesCheckNumPod createKubernetesCheckPodNumTask(
      String universeName,
      KubernetesCheckNumPod.CommandType commandType,
      String az,
      Map<String, String> config,
      int numPods,
      boolean isReadOnlyCluster) {
    KubernetesCheckNumPod.Params params = new KubernetesCheckNumPod.Params();
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster =
          Universe.getOrBadRequest(taskParams().getUniverseUUID())
              .getUniverseDetails()
              .getPrimaryCluster();
    }
    params.providerUUID = UUID.fromString(primaryCluster.userIntent.provider);
    params.commandType = commandType;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            az,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle);

    if (config != null) {
      params.config = config;
      // This assumes that the config is az config.
      // params.namespace remains null if config is not passed.
      params.namespace =
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              az,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    params.universeUUID = taskParams().getUniverseUUID();
    params.podNum = numPods;
    KubernetesCheckNumPod task = createTask(KubernetesCheckNumPod.class);
    task.initialize(params);
    return task;
  }

  protected void createPostUpgradeChecks(
      ServerType serverType, Collection<NodeDetails> nodeDetailsCollection) {
    if (isFollowerLagCheckEnabled()) {
      nodeDetailsCollection.stream()
          .forEach(
              nD ->
                  createCheckFollowerLagTask(nD, serverType)
                      .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse));
    }
  }

  protected SubTaskGroup addHandleKubernetesNamespacedServices(
      boolean readReplicaDelete,
      @Nullable UniverseDefinitionTaskParams universeParams,
      UUID universeUUID,
      boolean handleOwnershipChanges) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("HandleKubernetesNamespacedServices");
    HandleKubernetesNamespacedServices.Params params =
        new HandleKubernetesNamespacedServices.Params();
    params.setUniverseUUID(universeUUID);
    params.universeParams = universeParams;
    params.handleOwnershipChanges = handleOwnershipChanges;
    params.deleteReadReplica = readReplicaDelete;
    HandleKubernetesNamespacedServices task = createTask(HandleKubernetesNamespacedServices.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected void createValidateDiskSizeOnEdit(Universe universe) {
    int targetDiskUsagePercentage =
        confGetter.getConfForScope(universe, UniverseConfKeys.targetNodeDiskUsagePercentage);
    if (targetDiskUsagePercentage <= 0) {
      log.info(
          "Downsize disk size validation is disabled (usageMultiplierPercentage = {})",
          targetDiskUsagePercentage);
      return;
    }

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    for (Cluster newCluster : taskParams().clusters) {
      boolean isReadOnlyCluster = newCluster.clusterType == ClusterType.ASYNC;

      Cluster currCluster = universe.getCluster(newCluster.uuid);
      UserIntent newIntent = newCluster.userIntent;
      PlacementInfo newPI = newCluster.placementInfo, curPI = currCluster.placementInfo;
      Provider provider = Provider.getOrBadRequest(UUID.fromString(newIntent.provider));
      boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);

      KubernetesPlacement newPlacement = new KubernetesPlacement(newPI, isReadOnlyCluster),
          curPlacement = new KubernetesPlacement(curPI, isReadOnlyCluster);

      boolean masterChanged = true;
      boolean tserverChanged = true;

      Set<NodeDetails> tserversToDelete =
          getPodsToRemove(
              newPlacement.tservers,
              curPlacement.tservers,
              ServerType.TSERVER,
              universe,
              isMultiAZ,
              isReadOnlyCluster);

      Set<NodeDetails> mastersToDelete =
          getPodsToRemove(
              newPlacement.masters,
              curPlacement.masters,
              ServerType.MASTER,
              universe,
              isMultiAZ,
              isReadOnlyCluster);

      if (CollectionUtils.isEmpty(tserversToDelete) && CollectionUtils.isEmpty(mastersToDelete)) {
        log.debug("No pods to be removed");
      }

      DeviceInfo taskDeviceInfo = newCluster.userIntent.deviceInfo;
      DeviceInfo existingDeviceInfo = currCluster.userIntent.deviceInfo;
      if (taskDeviceInfo == null
          || existingDeviceInfo == null
          || (Objects.equals(taskDeviceInfo.numVolumes, existingDeviceInfo.numVolumes)
              && Objects.equals(taskDeviceInfo.volumeSize, existingDeviceInfo.volumeSize))) {
        log.debug("No change in the volume configuration");
        tserverChanged = CollectionUtils.isNotEmpty(tserversToDelete);
      }
      DeviceInfo taskMasterDeviceInfo = newCluster.userIntent.masterDeviceInfo;
      DeviceInfo existingMasterDeviceInfo = currCluster.userIntent.masterDeviceInfo;
      if (taskMasterDeviceInfo == null
          || existingMasterDeviceInfo == null
          || (Objects.equals(taskMasterDeviceInfo.numVolumes, existingMasterDeviceInfo.numVolumes)
              && Objects.equals(
                  taskMasterDeviceInfo.volumeSize, existingMasterDeviceInfo.volumeSize))) {
        log.debug("No change in the master volume configuration");
        masterChanged = CollectionUtils.isNotEmpty(mastersToDelete);
      }

      if (!masterChanged && !tserverChanged) {
        return;
      }

      // Gather all namespaces
      Set<String> namespaces = new HashSet<>();
      for (Entry<UUID, Map<String, String>> entry : newPlacement.configs.entrySet()) {
        UUID azUUID = entry.getKey();
        String azCode = isMultiAZ ? AvailabilityZone.get(azUUID).getCode() : null;
        Map<String, String> config = entry.getValue();
        String namespace =
            KubernetesUtil.getKubernetesNamespace(
                universeDetails.nodePrefix,
                azCode,
                config,
                universeDetails.useNewHelmNamingStyle,
                isReadOnlyCluster);
        namespaces.add(namespace);
      }

      SubTaskGroup validateSubTaskGroup =
          createSubTaskGroup(
              ValidateNodeDiskSize.class.getSimpleName(), SubTaskGroupType.ValidateConfigurations);
      ValidateNodeDiskSize.Params params =
          Json.fromJson(Json.toJson(taskParams()), ValidateNodeDiskSize.Params.class);
      params.clusterUuid = newCluster.uuid;
      params.namespaces = namespaces;
      params.tserversChanged = tserverChanged;
      params.mastersChanged = masterChanged;
      params.targetDiskUsagePercentage = targetDiskUsagePercentage;
      ValidateNodeDiskSize task = createTask(ValidateNodeDiskSize.class);
      task.initialize(params);
      validateSubTaskGroup.addSubTask(task);
      getRunnableTask().addSubTaskGroup(validateSubTaskGroup);
    }
  }

  protected Map<UUID, Map<ServerType, String>> getPerAZGflagsChecksumMap(
      String universeName, Cluster cluster) {
    Map<UUID, Map<ServerType, String>> perAZServerTypeGflagsChecksumMap = new HashMap<>();
    UserIntent userIntent = cluster.userIntent;
    PlacementInfo pI = cluster.placementInfo;
    boolean isReadOnlyCluster = cluster.clusterType == ClusterType.ASYNC;
    KubernetesPlacement newPlacement = new KubernetesPlacement(pI, isReadOnlyCluster);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;
    for (Entry<UUID, Map<String, String>> entry : newPlacement.configs.entrySet()) {
      UUID azUUID = entry.getKey();
      String azName =
          PlacementInfoUtil.isMultiAZ(provider)
              ? AvailabilityZone.getOrBadRequest(azUUID).getCode()
              : null;

      Map<String, String> azConfig = entry.getValue();
      String namespace =
          KubernetesUtil.getKubernetesNamespace(
              isMultiAZ,
              taskParams().nodePrefix,
              azName,
              azConfig,
              newNamingStyle,
              isReadOnlyCluster);

      String helmReleaseName =
          KubernetesUtil.getHelmReleaseName(
              isMultiAZ,
              taskParams().nodePrefix,
              universeName,
              azName,
              isReadOnlyCluster,
              newNamingStyle);
      Map<ServerType, String> previousGflagsChecksum =
          kubernetesManagerFactory
              .getManager()
              .getServerTypeGflagsChecksumMap(namespace, helmReleaseName, azConfig, newNamingStyle);
      perAZServerTypeGflagsChecksumMap.put(azUUID, previousGflagsChecksum);
    }
    return perAZServerTypeGflagsChecksumMap;
  }

  protected String getCertChecksum(Universe universe) {
    String certChecksum = null;
    if (universe.getUniverseDetails().getPrimaryCluster().getCertChecksum() != null) {
      return universe.getUniverseDetails().getPrimaryCluster().getCertChecksum();
    }
    // If cert checksum is not available in universe details, retrieve it from Kubernetes
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    PlacementInfo pI = universe.getUniverseDetails().getPrimaryCluster().placementInfo;
    boolean isReadOnlyCluster =
        universe.getUniverseDetails().getPrimaryCluster().clusterType == ClusterType.ASYNC;
    KubernetesPlacement placement = new KubernetesPlacement(pI, isReadOnlyCluster);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
    boolean newNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;

    // Get the first AZ config to retrieve cert checksum
    Entry<UUID, Map<String, String>> firstEntry = placement.configs.entrySet().iterator().next();
    if (firstEntry != null) {
      UUID azUUID = firstEntry.getKey();
      String azName = isMultiAZ ? AvailabilityZone.getOrBadRequest(azUUID).getCode() : null;
      Map<String, String> azConfig = firstEntry.getValue();

      String namespace =
          KubernetesUtil.getKubernetesNamespace(
              isMultiAZ,
              universe.getUniverseDetails().nodePrefix,
              azName,
              azConfig,
              newNamingStyle,
              isReadOnlyCluster);

      String helmReleaseName =
          KubernetesUtil.getHelmReleaseName(
              isMultiAZ,
              universe.getUniverseDetails().nodePrefix,
              universe.getName(),
              azName,
              isReadOnlyCluster,
              newNamingStyle);

      certChecksum =
          kubernetesManagerFactory
              .getManager()
              .getCertChecksum(namespace, helmReleaseName, azConfig);
    }
    return certChecksum;
  }
}
