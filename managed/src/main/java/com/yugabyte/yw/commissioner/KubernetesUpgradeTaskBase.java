// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.UpgradeTaskBase.MastersAndTservers;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.UpgradeContext;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.RollMaxBatchSize;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.UpgradeDetails;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public abstract class KubernetesUpgradeTaskBase extends KubernetesTaskBase {

  protected final OperatorStatusUpdater kubernetesStatus;

  protected KubernetesUpgradeTaskBase(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    this(baseTaskDependencies, operatorStatusUpdaterFactory, null);
  }

  protected KubernetesUpgradeTaskBase(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies, kubernetesManagerFactory);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
  }

  @Override
  protected UpgradeTaskParams taskParams() {
    return (UpgradeTaskParams) taskParams;
  }

  @Override
  protected boolean isBlacklistLeaders() {
    return getOrCreateExecutionContext().isBlacklistLeaders();
  }

  @Override
  protected boolean isSkipPrechecks() {
    return taskParams().skipNodeChecks;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    MastersAndTservers nodesToBeRestarted = getNodesToBeRestarted();
    log.debug("Nodes to be restarted {}", nodesToBeRestarted);
    if (taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE
        && nodesToBeRestarted != null
        && !nodesToBeRestarted.isEmpty()
        && !isSkipPrechecks()) {
      Optional<NodeDetails> nonLive =
          nodesToBeRestarted.getAllNodes().stream()
              .filter(n -> n.state != NodeDetails.NodeState.Live)
              .findFirst();
      if (!nonLive.isPresent()) {
        RollMaxBatchSize rollMaxBatchSize = getCurrentRollBatchSize(universe);
        // Use only primary nodes
        MastersAndTservers forCluster =
            nodesToBeRestarted.getForCluster(
                universe.getUniverseDetails().getPrimaryCluster().uuid);

        if (!forCluster.isEmpty()) {
          List<MastersAndTservers> split =
              UpgradeTaskBase.split(universe, forCluster, rollMaxBatchSize);
          createCheckNodesAreSafeToTakeDownTask(split, getTargetSoftwareVersion(), true);
        }
      }
    }
  }

  @Override
  protected void addBasicPrecheckTasks() {
    if (isFirstTry() && !isSkipPrechecks()) {
      verifyClustersConsistency();
    }
  }

  protected String getTargetSoftwareVersion() {
    return null;
  }

  private MastersAndTservers nodesToBeRestarted;

  // Override in child classes if required
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodes(UpgradeOption.ROLLING_UPGRADE /* Only Rolling support in K8s */);
  }

  protected final MastersAndTservers getNodesToBeRestarted() {
    if (nodesToBeRestarted == null) {
      nodesToBeRestarted = calculateNodesToBeRestarted();
    }
    return nodesToBeRestarted;
  }

  protected MastersAndTservers fetchNodes(UpgradeOption upgradeOption) {
    return new MastersAndTservers(
        fetchMasterNodes(getUniverse(), upgradeOption),
        fetchTServerNodes(getUniverse(), upgradeOption));
  }

  private List<NodeDetails> fetchMasterNodes(Universe universe, UpgradeOption upgradeOption) {
    List<NodeDetails> masterNodes = universe.getMasters();
    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      final String leaderMasterAddress = universe.getMasterLeaderHostText();
      return UpgradeTaskBase.sortMastersInRestartOrder(universe, leaderMasterAddress, masterNodes);
    }
    return masterNodes;
  }

  private List<NodeDetails> fetchTServerNodes(Universe universe, UpgradeOption upgradeOption) {
    List<NodeDetails> tServerNodes = universe.getTServers();
    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      return UpgradeTaskBase.sortTServersInRestartOrder(universe, tServerNodes);
    }
    return tServerNodes;
  }

  public abstract SubTaskGroupType getTaskSubGroupType();

  private RollMaxBatchSize getCurrentRollBatchSize(Universe universe) {
    return getCurrentRollBatchSize(universe, taskParams().rollMaxBatchSize);
  }

  public void runUpgrade(Runnable upgradeLambda) {
    runUpgrade(upgradeLambda, null /* onFailureTask */);
  }

  // Wrapper that takes care of common pre and post upgrade tasks and user has
  // flexibility to manipulate subTaskGroupQueue through the lambda passed in parameter
  public void runUpgrade(Runnable upgradeLambda, Runnable onFailureTask) {
    Throwable th = null;
    if (maybeRunOnlyPrechecks()) {
      return;
    }
    checkUniverseVersion();
    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, null /* Txn callback */);
    kubernetesStatus.startYBUniverseEventStatus(
        universe,
        taskParams().getKubernetesResourceDetails(),
        getTaskExecutor().getTaskType(getClass()).name(),
        getUserTaskUUID(),
        UniverseState.EDITING);
    try {
      if (taskParams().nodePrefix == null) {
        taskParams().nodePrefix = universe.getUniverseDetails().nodePrefix;
      }
      if (taskParams().clusters == null || taskParams().clusters.isEmpty()) {
        taskParams().clusters = universe.getUniverseDetails().clusters;
      }

      // This value is used by subsequent calls to helper methods for
      // creating KubernetesCommandExecutor tasks. This value cannot
      // be changed once set during the Universe creation, so we don't
      // allow users to modify it later during edit, upgrade, etc.
      taskParams().useNewHelmNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;

      // Execute the lambda which populates subTaskGroupQueue
      upgradeLambda.run();

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error={}.", getName(), t);
      if (taskParams().getUniverseSoftwareUpgradeStateOnFailure() != null) {
        universe = getUniverse();
        if (!UniverseDefinitionTaskParams.IN_PROGRESS_UNIV_SOFTWARE_UPGRADE_STATES.contains(
            universe.getUniverseDetails().softwareUpgradeState)) {
          log.debug("Skipping universe upgrade state as actual task was not started.");
        } else {
          universe.updateUniverseSoftwareUpgradeState(
              taskParams().getUniverseSoftwareUpgradeStateOnFailure());
          log.debug(
              "Updated universe {} software upgrade state to  {}.",
              taskParams().getUniverseUUID(),
              taskParams().getUniverseSoftwareUpgradeStateOnFailure());
        }
      }
      // If the task failed, we don't want the loadbalancer to be
      // disabled, so we enable it again in case of errors.
      setTaskQueueAndRun(
          () -> createLoadBalancerStateChangeTask(true).setSubTaskGroupType(getTaskSubGroupType()));
      if (onFailureTask != null) {
        log.info("Running on failure upgrade task");
        onFailureTask.run();
        log.info("Finished on failure upgrade task");
      }
      th = t;
      throw t;
    } finally {
      try {
        setTaskQueueAndRun(
            () -> clearLeaderBlacklistIfAvailable(SubTaskGroupType.ConfigureUniverse));
        if (taskParams().upgradeOption.equals(UpgradeOption.NON_ROLLING_UPGRADE)) {
          // Add logic for changing update-strategy here too.
          getRunnableTask().reset();
        }
      } finally {
        try {
          unlockXClusterUniverses(lockedXClusterUniversesUuidSet, false /* ignoreErrors */);
        } finally {
          kubernetesStatus.updateYBUniverseStatus(
              getUniverse(),
              taskParams().getKubernetesResourceDetails(),
              getTaskExecutor().getTaskType(getClass()).name(),
              getUserTaskUUID(),
              (th != null) ? UniverseState.ERROR_UPDATING : UniverseState.READY,
              th);
          unlockUniverseForUpdate();
        }
      }
    }

    log.info("Finished {} task.", getName());
  }

  public void createUpgradeTask(
      Universe universe, String softwareVersion, boolean upgradeMasters, boolean upgradeTservers) {
    createUpgradeTask(universe, softwareVersion, upgradeMasters, upgradeTservers, false, null);
  }

  public void createUpgradeTask(
      Universe universe,
      String softwareVersion,
      boolean upgradeMasters,
      boolean upgradeTservers,
      boolean enableYbc,
      String ybcSoftwareVersion,
      UpgradeContext upgradeContext) {
    createUpgradeTask(
        universe,
        softwareVersion,
        upgradeMasters,
        upgradeTservers,
        CommandType.HELM_UPGRADE,
        enableYbc,
        ybcSoftwareVersion,
        upgradeContext);
  }

  public void createUpgradeTask(
      Universe universe,
      String softwareVersion,
      boolean upgradeMasters,
      boolean upgradeTservers,
      boolean enableYbc,
      String ybcSoftwareVersion) {
    createUpgradeTask(
        universe,
        softwareVersion,
        upgradeMasters,
        upgradeTservers,
        CommandType.HELM_UPGRADE,
        enableYbc,
        ybcSoftwareVersion);
  }

  public void createUpgradeTask(
      Universe universe,
      String softwareVersion,
      boolean upgradeMasters,
      boolean upgradeTservers,
      CommandType commandType,
      boolean enableYbc,
      String ybcSoftwareVersion) {
    createUpgradeTask(
        universe,
        softwareVersion,
        upgradeMasters,
        upgradeTservers,
        commandType,
        enableYbc,
        ybcSoftwareVersion,
        null);
  }

  public void createUpgradeTask(
      Universe universe,
      String softwareVersion,
      boolean upgradeMasters,
      boolean upgradeTservers,
      CommandType commandType,
      boolean enableYbc,
      String ybcSoftwareVersion,
      @Nullable UpgradeContext upgradeContext) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    Cluster primaryCluster = universeDetails.getPrimaryCluster();
    PlacementInfo placementInfo = primaryCluster.getOverallPlacement();
    createSingleKubernetesExecutorTask(
        universe.getName(), CommandType.POD_INFO, placementInfo, /*isReadOnlyCluster*/ false);

    KubernetesPlacement placement =
        new KubernetesPlacement(placementInfo, /*isReadOnlyCluster*/ false);
    Provider provider =
        Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));
    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

    String universeOverrides = primaryCluster.userIntent.universeOverrides;
    Map<String, String> azOverrides = primaryCluster.userIntent.azOverrides;
    if (azOverrides == null) {
      azOverrides = new HashMap<String, String>();
    }

    String masterAddresses =
        KubernetesUtil.computeMasterAddresses(
            placementInfo,
            placement.masters,
            taskParams().nodePrefix,
            universe.getName(),
            provider,
            universeDetails.communicationPorts.masterRpcPort,
            newNamingStyle);

    boolean tserverFirst = (upgradeContext != null && upgradeContext.isProcessTServersFirst());
    YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState =
        upgradeContext != null ? upgradeContext.getYsqlMajorVersionUpgradeState() : null;
    UUID rootCAUUID = upgradeContext != null ? upgradeContext.getRootCAUUID() : null;

    // If upgradeContext is non-null and has non-null useYBDBInbuiltYbc we use that.
    // It will be set for KubernetesToggleImmutableYbc task.
    // Otherwise pick from universe primary cluster userIntent.
    boolean useYBDBInbuiltYbc =
        (upgradeContext != null && upgradeContext.getUseYBDBInbuiltYbc() != null)
            ? upgradeContext.getUseYBDBInbuiltYbc()
            : universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc();
    if (upgradeMasters && !tserverFirst) {
      upgradePodsTask(
          universe.getName(),
          placement,
          masterAddresses,
          null,
          ServerType.MASTER,
          softwareVersion,
          universeOverrides,
          azOverrides,
          newNamingStyle,
          /*isReadOnlyCluster*/ false,
          commandType,
          enableYbc,
          ybcSoftwareVersion,
          PodUpgradeParams.builder()
              .delayAfterStartup(taskParams().sleepAfterMasterRestartMillis)
              .build(),
          ysqlMajorVersionUpgradeState,
          rootCAUUID);
    }

    if (upgradeTservers) {
      if (!isBlacklistLeaders()) {
        createLoadBalancerStateChangeTask(false).setSubTaskGroupType(getTaskSubGroupType());
      }

      upgradePodsTask(
          universe.getName(),
          placement,
          masterAddresses,
          null,
          ServerType.TSERVER,
          softwareVersion,
          universeOverrides,
          azOverrides,
          newNamingStyle,
          /*isReadOnlyCluster*/ false,
          commandType,
          enableYbc,
          ybcSoftwareVersion,
          PodUpgradeParams.builder()
              .delayAfterStartup(taskParams().sleepAfterTServerRestartMillis)
              .rollMaxBatchSize(getCurrentRollBatchSize(universe))
              .build(),
          ysqlMajorVersionUpgradeState,
          rootCAUUID);

      if (enableYbc) {
        Set<NodeDetails> primaryTservers = new HashSet<>(universe.getTServersInPrimaryCluster());
        if (!useYBDBInbuiltYbc) {
          installYbcOnThePods(
              primaryTservers,
              false,
              ybcSoftwareVersion,
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybcFlags);
          performYbcAction(primaryTservers, false, "stop");
        } else {
          log.debug("Skipping configure YBC as 'useYBDBInbuiltYbc' is enabled");
        }
        createWaitForYbcServerTask(primaryTservers);
      }

      // Handle read cluster upgrade.
      if (universeDetails.getReadOnlyClusters().size() != 0) {
        Cluster asyncCluster = universeDetails.getReadOnlyClusters().get(0);
        PlacementInfo readClusterPlacementInfo = asyncCluster.getOverallPlacement();
        createSingleKubernetesExecutorTask(
            universe.getName(),
            CommandType.POD_INFO,
            readClusterPlacementInfo, /*isReadOnlyCluster*/
            true);

        KubernetesPlacement readClusterPlacement =
            new KubernetesPlacement(readClusterPlacementInfo, /*isReadOnlyCluster*/ true);

        upgradePodsTask(
            universe.getName(),
            readClusterPlacement,
            masterAddresses,
            null,
            ServerType.TSERVER,
            softwareVersion,
            universeOverrides,
            azOverrides,
            newNamingStyle,
            /*isReadOnlyCluster*/ true,
            commandType,
            enableYbc,
            ybcSoftwareVersion,
            PodUpgradeParams.builder()
                .delayAfterStartup(taskParams().sleepAfterTServerRestartMillis)
                .rollMaxBatchSize(getCurrentRollBatchSize(universe))
                .build(),
            ysqlMajorVersionUpgradeState,
            rootCAUUID);

        if (enableYbc) {
          Set<NodeDetails> replicaTservers =
              new HashSet<NodeDetails>(universe.getNodesInCluster(asyncCluster.uuid));
          if (!useYBDBInbuiltYbc) {
            installYbcOnThePods(
                replicaTservers, true, ybcSoftwareVersion, asyncCluster.userIntent.ybcFlags);
            performYbcAction(replicaTservers, true, "stop");
          }
          createWaitForYbcServerTask(replicaTservers);
        }
      }
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(getTaskSubGroupType());
    }

    if (upgradeMasters && tserverFirst) {
      upgradePodsTask(
          universe.getName(),
          placement,
          masterAddresses,
          null,
          ServerType.MASTER,
          softwareVersion,
          universeOverrides,
          azOverrides,
          newNamingStyle,
          /*isReadOnlyCluster*/ false,
          commandType,
          enableYbc,
          ybcSoftwareVersion,
          PodUpgradeParams.builder()
              .delayAfterStartup(taskParams().sleepAfterMasterRestartMillis)
              .build(),
          ysqlMajorVersionUpgradeState,
          rootCAUUID);
    }
  }

  public void createNonRollingUpgradeTask(
      Universe universe,
      String softwareVersion,
      boolean isMasterChanged,
      boolean isTServerChanged,
      boolean enableYbc,
      String ybcSoftwareVersion) {
    createNonRollingUpgradeTask(
        universe,
        softwareVersion,
        isMasterChanged,
        isTServerChanged,
        enableYbc,
        ybcSoftwareVersion,
        null /* upgradeContext */);
  }

  public void createNonRollingUpgradeTask(
      Universe universe,
      String softwareVersion,
      boolean isMasterChanged,
      boolean isTServerChanged,
      boolean enableYbc,
      String ybcSoftwareVersion,
      @Nullable UpgradeContext upgradeContext) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    Cluster primaryCluster = universeDetails.getPrimaryCluster();
    PlacementInfo placementInfo = primaryCluster.getOverallPlacement();
    createSingleKubernetesExecutorTask(
        universe.getName(), CommandType.POD_INFO, placementInfo, /*isReadOnlyCluster*/ false);

    KubernetesPlacement placement =
        new KubernetesPlacement(placementInfo, /*isReadOnlyCluster*/ false);
    Provider provider =
        Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));
    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

    String universeOverrides = primaryCluster.userIntent.universeOverrides;
    Map<String, String> azOverrides = primaryCluster.userIntent.azOverrides;
    if (azOverrides == null) {
      azOverrides = new HashMap<String, String>();
    }
    UUID rootCAUUID = upgradeContext != null ? upgradeContext.getRootCAUUID() : null;

    // If upgradeContext is non-null and has non-null useYBDBInbuiltYbc we use that.
    // It will be set for KubernetesToggleImmutableYbc task.
    // Otherwise pick from universe primary cluster userIntent.
    boolean useYBDBInbuiltYbc =
        (upgradeContext != null && upgradeContext.getUseYBDBInbuiltYbc() != null)
            ? upgradeContext.getUseYBDBInbuiltYbc()
            : universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc();

    String masterAddresses =
        KubernetesUtil.computeMasterAddresses(
            placementInfo,
            placement.masters,
            taskParams().nodePrefix,
            universe.getName(),
            provider,
            universeDetails.communicationPorts.masterRpcPort,
            newNamingStyle);
    ServerType serverType =
        (isMasterChanged && isTServerChanged)
            ? (ServerType.EITHER)
            : (isMasterChanged
                ? ServerType.MASTER
                : (isTServerChanged ? ServerType.TSERVER : null));
    if (serverType != null) {
      upgradePodsNonRolling(
          universe.getName(),
          placement,
          masterAddresses,
          serverType,
          softwareVersion,
          universeOverrides,
          azOverrides,
          newNamingStyle,
          /*isReadOnlyCluster*/ false,
          enableYbc,
          null /* ybcSoftwareVersion */,
          null /* ysqlMajorVersionUpgradeState */,
          rootCAUUID);
    }

    if (isTServerChanged) {
      if (enableYbc) {
        Set<NodeDetails> primaryTservers =
            new HashSet<NodeDetails>(universe.getTServersInPrimaryCluster());
        if (!useYBDBInbuiltYbc) {
          installYbcOnThePods(
              primaryTservers,
              false,
              ybcSoftwareVersion,
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybcFlags);
          performYbcAction(primaryTservers, false, "stop");
        } else {
          log.debug("Skipping configure YBC as 'useYBDBInbuiltYbc' is enabled");
        }
        createWaitForYbcServerTask(primaryTservers);
      }

      // Handle read cluster upgrade.
      if (universeDetails.getReadOnlyClusters().size() != 0) {
        PlacementInfo readClusterPlacementInfo =
            universeDetails.getReadOnlyClusters().get(0).getOverallPlacement();
        createSingleKubernetesExecutorTask(
            universe.getName(),
            CommandType.POD_INFO,
            readClusterPlacementInfo, /*isReadOnlyCluster*/
            true);

        KubernetesPlacement readClusterPlacement =
            new KubernetesPlacement(readClusterPlacementInfo, /*isReadOnlyCluster*/ true);

        upgradePodsNonRolling(
            universe.getName(),
            readClusterPlacement,
            masterAddresses,
            ServerType.TSERVER,
            softwareVersion,
            universeOverrides,
            azOverrides,
            newNamingStyle,
            /*isReadOnlyCluster*/ true,
            enableYbc,
            ybcSoftwareVersion,
            null /* ysqlMajorVersionUpgradeState */,
            rootCAUUID);

        if (enableYbc) {
          Set<NodeDetails> replicaTservers =
              new HashSet<NodeDetails>(
                  universe.getNodesInCluster(
                      universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid));
          if (!useYBDBInbuiltYbc) {
            installYbcOnThePods(
                replicaTservers,
                true,
                ybcSoftwareVersion,
                universeDetails.getReadOnlyClusters().get(0).userIntent.ybcFlags);
            performYbcAction(replicaTservers, true, "stop");
          }
          createWaitForYbcServerTask(replicaTservers);
        }
      }
    }
  }

  @FunctionalInterface
  protected interface PostClusterUpgradeTask {
    void executePostClusterUpgradeTask(boolean isPrimary);
  }

  protected void createNonRestartUpgradeTask(Universe universe) {
    createNonRestartUpgradeTask(universe, null /* upgradeContext */);
  }

  protected void createNonRestartUpgradeTask(
      Universe universe, @Nullable UpgradeContext upgradeContext) {
    createNonRestartUpgradeTask(universe, upgradeContext, null /* postClusterUpgradeTask */);
  }

  protected void createNonRestartUpgradeTask(
      Universe universe,
      @Nullable UpgradeContext upgradeContext,
      @Nullable PostClusterUpgradeTask postClusterUpgradeTask) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    Cluster primaryCluster = universeDetails.getPrimaryCluster();
    KubernetesUpgradeCommonParams upgradeParamsPrimary =
        new KubernetesUpgradeCommonParams(universe, primaryCluster, confGetter);
    createSingleKubernetesExecutorTask(
        universe.getName(),
        CommandType.POD_INFO,
        primaryCluster.getOverallPlacement(),
        false /*isReadOnlyCluster*/);

    UUID rootCAUUID = upgradeContext != null ? upgradeContext.getRootCAUUID() : null;

    upgradePodsNonRestart(
        universe.getName(),
        upgradeParamsPrimary.getPlacement(),
        upgradeParamsPrimary.getMasterAddresses(),
        ServerType.EITHER,
        upgradeParamsPrimary.getYbSoftwareVersion(),
        upgradeParamsPrimary.getUniverseOverrides(),
        upgradeParamsPrimary.getAzOverrides(),
        upgradeParamsPrimary.isNewNamingStyle(),
        false /* isReadOnlyCluster */,
        upgradeParamsPrimary.isEnableYbc(),
        upgradeParamsPrimary.getYbcSoftwareVersion(),
        null /* ysqlMajorVersionUpgradeState */,
        rootCAUUID);

    // Execute post upgrade task for primary cluster if provided
    if (postClusterUpgradeTask != null) {
      postClusterUpgradeTask.executePostClusterUpgradeTask(true /* isPrimary */);
    }

    if (universeDetails.getReadOnlyClusters().size() != 0) {
      Cluster readOnlyCluster = universeDetails.getReadOnlyClusters().get(0);
      KubernetesUpgradeCommonParams upgradeParamsReadOnly =
          new KubernetesUpgradeCommonParams(universe, readOnlyCluster, confGetter);
      PlacementInfo readClusterPlacementInfo = readOnlyCluster.getOverallPlacement();
      createSingleKubernetesExecutorTask(
          universe.getName(),
          CommandType.POD_INFO,
          readClusterPlacementInfo,
          true /*isReadOnlyCluster*/);

      upgradePodsNonRestart(
          universe.getName(),
          upgradeParamsReadOnly.getPlacement(),
          upgradeParamsReadOnly.getMasterAddresses(),
          ServerType.TSERVER,
          upgradeParamsReadOnly.getYbSoftwareVersion(),
          upgradeParamsReadOnly.getUniverseOverrides(),
          upgradeParamsReadOnly.getAzOverrides(),
          upgradeParamsReadOnly.isNewNamingStyle(),
          true /* isReadOnlyCluster */,
          upgradeParamsReadOnly.isEnableYbc(),
          upgradeParamsReadOnly.getYbcSoftwareVersion(),
          null /* ysqlMajorVersionUpgradeState */,
          rootCAUUID);

      // Execute post upgrade task for read-only cluster if provided
      if (postClusterUpgradeTask != null) {
        postClusterUpgradeTask.executePostClusterUpgradeTask(false /* isPrimary */);
      }
    }
  }

  protected void createNonRestartGflagsUpgradeTask(Universe universe) {
    createNonRestartUpgradeTask(
        universe,
        null /* upgradeContext */,
        (isPrimary) -> {
          List<Cluster> newClusters = taskParams().clusters;
          MastersAndTservers mastersAndTservers = fetchNodes(UpgradeOption.NON_RESTART_UPGRADE);
          Cluster cluster;

          if (isPrimary) {
            cluster = taskParams().getPrimaryCluster();
            MastersAndTservers primaryClusterMastersAndTservers =
                mastersAndTservers.getForCluster(
                    universe.getUniverseDetails().getPrimaryCluster().uuid);

            createSetFlagInMemoryTasks(
                primaryClusterMastersAndTservers.mastersList,
                ServerType.MASTER,
                (node, params) -> {
                  params.force = true;
                  params.gflags =
                      GFlagsUtil.getGFlagsForNode(node, ServerType.MASTER, cluster, newClusters);
                });
            createSetFlagInMemoryTasks(
                primaryClusterMastersAndTservers.tserversList,
                ServerType.TSERVER,
                (node, params) -> {
                  params.force = true;
                  params.gflags =
                      GFlagsUtil.getGFlagsForNode(node, ServerType.TSERVER, cluster, newClusters);
                });
          } else {
            cluster = taskParams().getReadOnlyClusters().get(0);
            createSetFlagInMemoryTasks(
                mastersAndTservers.getForCluster(cluster.uuid).tserversList,
                ServerType.TSERVER,
                (node, params) -> {
                  params.force = true;
                  params.gflags =
                      GFlagsUtil.getGFlagsForNode(node, ServerType.TSERVER, cluster, newClusters);
                });
          }
        });
  }

  protected void createSoftwareUpgradePrecheckTasks(
      String ybSoftwareVersion, boolean ysqlMajorVersionUpgrade) {
    createCheckUpgradeTask(ybSoftwareVersion);
    // Skip PG Upgrade check on tserver nodes if it is an retry task.
    // Pre-check will still be executed after the master upgrade as part of main task.
    if (ysqlMajorVersionUpgrade && taskParams().getPreviousTaskUUID() == null) {
      createPGUpgradeTServerCheckTask(ybSoftwareVersion);
    }
  }

  protected void createGFlagsUpgradeAndUpdateMastersTaskForYSQLMajorUpgrade(
      Universe universe,
      String softwareVersion,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {

    createSetYBMajorVersionUpgradeCompatibility(
        universe,
        ServerType.MASTER,
        universe.getMasters(),
        UpgradeDetails.getMajorUpgradeCompatibilityFlagValue(ysqlMajorVersionUpgradeState));

    createSetYBMajorVersionUpgradeCompatibility(
        universe,
        ServerType.TSERVER,
        universe.getTServers(),
        UpgradeDetails.getMajorUpgradeCompatibilityFlagValue(ysqlMajorVersionUpgradeState));

    createServerConfUpdateTaskAndUpdateMasterForYsqlMajorUpgrade(
        universe, universe.getTServers(), softwareVersion, ysqlMajorVersionUpgradeState);
  }

  private void createServerConfUpdateTaskAndUpdateMasterForYsqlMajorUpgrade(
      Universe universe,
      List<NodeDetails> nodes,
      String softwareVersion,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      KubernetesUpgradeCommonParams upgradeParams =
          new KubernetesUpgradeCommonParams(universe, cluster, confGetter);
      // override the software version to the new version
      upgradeParams.setYbSoftwareVersion(softwareVersion);
      createSingleKubernetesExecutorTask(
          universe.getName(),
          CommandType.POD_INFO,
          cluster.getOverallPlacement(),
          false /*isReadOnlyCluster*/);
      // Helm upgrade
      upgradePodsNonRestart(
          universe.getName(),
          upgradeParams.getPlacement(),
          upgradeParams.getMasterAddresses(),
          ServerType.EITHER,
          upgradeParams.getYbSoftwareVersion(),
          upgradeParams.getUniverseOverrides(),
          upgradeParams.getAzOverrides(),
          upgradeParams.isNewNamingStyle(),
          false /* isReadOnlyCluster */,
          upgradeParams.isEnableYbc(),
          upgradeParams.getYbcSoftwareVersion(),
          ysqlMajorVersionUpgradeState);
    }
  }
}
