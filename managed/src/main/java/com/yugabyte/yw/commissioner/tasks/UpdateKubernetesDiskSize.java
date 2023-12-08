/*
* Copyright 2022 YugaByte, Inc. and Contributors
*
* Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
* may not use this file except in compliance with the License. You
* may obtain a copy of the License at
*
https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.operator.KubernetesOperatorStatusUpdater;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateKubernetesDiskSize extends EditKubernetesUniverse {

  @Inject
  protected UpdateKubernetesDiskSize(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesOperatorStatusUpdater kubernetesStatus,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies, kubernetesStatus);
  }

  @Override
  protected ResizeNodeParams taskParams() {
    return (ResizeNodeParams) taskParams;
  }

  @Override
  public void run() {
    try {
      checkUniverseVersion();
      verifyParams(UniverseOpType.EDIT);
      // additional verification about disk size increase is needed here

      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      taskParams().useNewHelmNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;
      preTaskActions();

      // String softwareVersion = userIntent.ybSoftwareVersion;
      // primary and readonly clusters disk resize
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
        // run the disk resize tasks for each AZ in the Cluster
        createResizeDiskTask(
            universe.getName(),
            placement,
            masterAddresses,
            newIntent,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle,
            universe.isYbcEnabled(),
            universe.getUniverseDetails().getYbcSoftwareVersion());

        // persist the changes to the universe
        createPersistResizeNodeTask(cluster.userIntent, cluster.uuid);
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
