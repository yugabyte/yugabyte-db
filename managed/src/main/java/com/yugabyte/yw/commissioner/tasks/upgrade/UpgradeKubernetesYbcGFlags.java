// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.YbcGflagsTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@Abortable
public class UpgradeKubernetesYbcGFlags extends KubernetesUpgradeTaskBase {

  private final YbcManager ybcManager;

  @Override
  protected YbcGflagsTaskParams taskParams() {
    return (YbcGflagsTaskParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingYbcGFlags;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()) {
      super.createPrecheckTasks(universe);
      addBasicPrecheckTasks();
    }
  }

  @Inject
  protected UpgradeKubernetesYbcGFlags(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      ReleaseManager releaseManager,
      YbcManager ybcManager) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
    this.ybcManager = ybcManager;
  }

  public void run() {
    log.info("Running {}", getName());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Map<String, String> ybcGflagsMap = null;
    try {
      ybcGflagsMap = YbcManager.convertYbcFlagsToMap(taskParams().ybcGflags);
    } catch (IllegalAccessException e) {
      throw new RuntimeException(e);
    }

    // K8s universes with immmutable YBC enabled need rolling/non-rolling helm upgrade
    // to modify YBC gflags. For non-immutable YBC, we perform configure by copying package
    // and placing gflags file.
    boolean immutableYbc =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc();
    runUpgrade(
        immutableYbc
            ? immutableYbcGflagsUpgrade(universe, ybcGflagsMap)
            : nonImmutableYbcGflagsUpgrade(universe, ybcGflagsMap));
  }

  private Runnable nonImmutableYbcGflagsUpgrade(
      Universe universe, Map<String, String> ybcGflagsMap) {
    return () -> {
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      Set<NodeDetails> nodeDetailSet =
          new HashSet<>(universe.getTserversInCluster(universeDetails.getPrimaryCluster().uuid));

      installYbcOnThePods(nodeDetailSet, false, ybcManager.getStableYbcVersion(), ybcGflagsMap);
      performYbcAction(nodeDetailSet, false, "stop");
      createWaitForYbcServerTask(nodeDetailSet)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      List<Cluster> readOnlyClusters = universeDetails.getReadOnlyClusters();
      if (!readOnlyClusters.isEmpty()) {
        nodeDetailSet = universeDetails.getTserverNodesInCluster(readOnlyClusters.get(0).uuid);
        installYbcOnThePods(nodeDetailSet, true, ybcManager.getStableYbcVersion(), ybcGflagsMap);
        performYbcAction(nodeDetailSet, true, "stop");
        createWaitForYbcServerTask(nodeDetailSet)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
      // Persist in the DB.
      createUpdateYbcGFlagInTheUniverseDetailsTask(ybcGflagsMap)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    };
  }

  private Runnable immutableYbcGflagsUpgrade(Universe universe, Map<String, String> ybcGflagsMap) {
    return () -> {
      Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
      String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);

      taskParams().getPrimaryCluster().userIntent.ybcFlags = ybcGflagsMap;

      // Create Kubernetes Upgrade Task.
      switch (taskParams().upgradeOption) {
        case ROLLING_UPGRADE:
          createUpgradeTask(
              getUniverse(),
              cluster.userIntent.ybSoftwareVersion,
              false /* upgradeMasters */, // YBC is only on tservers
              true /* upgradeTservers */,
              universe.isYbcEnabled(),
              stableYbcVersion);
          break;
        case NON_ROLLING_UPGRADE:
          createNonRollingUpgradeTask(
              getUniverse(),
              cluster.userIntent.ybSoftwareVersion,
              false /* upgradeMasters */, // YBC is only on tservers
              true /* upgradeTservers */,
              universe.isYbcEnabled(),
              stableYbcVersion);
          break;
        default:
          throw new RuntimeException("Unsupported upgrade option!");
      }
      // Persist in the DB.
      createUpdateYbcGFlagInTheUniverseDetailsTask(ybcGflagsMap)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    };
  }
}
