/*
* Copyright 2022 YugabyteDB, Inc. and Contributors
*
* Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
* may not use this file except in compliance with the License. You
* may obtain a copy of the License at
*
https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/
package com.yugabyte.yw.commissioner.tasks;

import com.nimbusds.oauth2.sdk.util.MapUtils;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class UpdateKubernetesDiskSize extends EditKubernetesUniverse {

  private YbcManager ybcManager;

  @Inject
  protected UpdateKubernetesDiskSize(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      YbcManager ybcManager) {
    super(baseTaskDependencies, kubernetesManagerFactory, operatorStatusUpdaterFactory, ybcManager);
  }

  @Override
  protected ResizeNodeParams taskParams() {
    return (ResizeNodeParams) taskParams;
  }

  @Override
  protected boolean isSkipPrechecks() {
    return true;
  }

  @Override
  public void run() {
    try {
      checkUniverseVersion();
      verifyParams(UniverseOpType.EDIT);
      // additional verification about disk size increase is needed here

      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);
      taskParams().useNewHelmNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;
      preTaskActions();

      // String softwareVersion = userIntent.ybSoftwareVersion;
      // primary and readonly clusters disk resize
      boolean usePreviousGflagsChecksum =
          KubernetesUtil.isNonRestartGflagsUpgradeSupported(
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
      for (UniverseDefinitionTaskParams.Cluster cluster : taskParams().clusters) {
        Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
        boolean isReadOnlyCluster =
            cluster.clusterType == UniverseDefinitionTaskParams.ClusterType.ASYNC;
        KubernetesPlacement placement =
            new KubernetesPlacement(cluster.placementInfo, isReadOnlyCluster);
        String masterAddresses =
            KubernetesUtil.computeMasterAddresses(
                cluster.placementInfo,
                placement.masters,
                taskParams().nodePrefix,
                universe.getName(),
                provider,
                universe.getUniverseDetails().communicationPorts.masterRpcPort,
                taskParams().useNewHelmNamingStyle);
        UserIntent newIntent = taskParams().getPrimaryCluster().userIntent;
        UserIntent curIntent =
            universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;
        // Update disk size if there is a change
        Map<UUID, Pair<Boolean, Boolean>> azToDiskSizeChangeMap =
            canResizeDisk(placement, placement, newIntent, curIntent, taskParams().nodeDetailsSet);
        // run the disk resize tasks for each AZ in the Cluster
        if (MapUtils.isNotEmpty(azToDiskSizeChangeMap)) {
          createResizeDiskTask(
              universe.getName(),
              placement,
              cluster.uuid,
              masterAddresses,
              newIntent,
              isReadOnlyCluster,
              taskParams().useNewHelmNamingStyle,
              universe.isYbcEnabled(),
              confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion),
              azToDiskSizeChangeMap,
              usePreviousGflagsChecksum,
              null /* skipMasterAZs */,
              null /* skipTserverAZs */);
        } else {
          log.warn("Skipping disk resize as there are no AZs which can undergo disk resize!");
        }
      }

      // Marks update of this universe as a success only if all the tasks before it
      // succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
