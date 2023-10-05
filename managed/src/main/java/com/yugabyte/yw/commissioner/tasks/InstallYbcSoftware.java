// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static java.util.stream.Collectors.toList;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class InstallYbcSoftware extends UniverseDefinitionTaskBase {

  @Inject private ReleaseManager releaseManager;
  @Inject private YbcManager ybcManager;

  @Inject
  protected InstallYbcSoftware(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening. Does not alter 'updateSucceeded' flag so as not
      // to lock out the universe completely in case this task fails.
      lockUniverse(-1 /* expectedUniverseVersion */);

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

      // Check whether the target ybc version is present in YB-Anywhere for each node.
      universe
          .getNodes()
          .forEach(
              (node) -> {
                Pair<String, String> ybcPackageDetails =
                    ybcManager.getYbcPackageDetailsForNode(universe, node);
                if (releaseManager.getYbcReleaseByVersion(
                        taskParams().getYbcSoftwareVersion(),
                        ybcPackageDetails.getFirst(),
                        ybcPackageDetails.getSecond())
                    == null) {
                  throw new RuntimeException(
                      "Target ybc package "
                          + taskParams().getYbcSoftwareVersion()
                          + " does not exists for node"
                          + node.nodeName);
                }
              });

      // We will need to setup server again in case of systemd to register yb-controller service.
      if (!universe.isYbcEnabled()
          && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd) {
        // We assume that user has provisioned nodes again with new service files in case of
        // on-prem manual provisioned universe.
        if (!Util.isOnPremManualProvisioning(universe)) {
          createSetupServerTasks(universe.getNodes(), param -> param.isSystemdUpgrade = true);
        }
      }

      // create task for installing yb-controller on each DB node.
      createYbcSoftwareInstallTasks(
          universe.getNodes().stream().collect(toList()), null, SubTaskGroupType.UpgradingSoftware);

      // Start yb-controller process and wait for it to get responsive.
      createStartYbcProcessTasks(
          new HashSet<>(universe.getNodes()),
          universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);

      //  Update Universe detail to enable yb-controller.
      createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Set the node states to Live.
      createSetNodeStateTasks(universe.getNodes(), NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
