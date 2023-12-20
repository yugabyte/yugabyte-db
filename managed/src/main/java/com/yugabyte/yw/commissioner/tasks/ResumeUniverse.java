/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ResumeUniverse extends UniverseTaskBase {

  @Inject
  protected ResumeUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */);

      if (!universe.getUniverseDetails().isImportedUniverse()) {
        // Create tasks to resume the existing nodes.
        createResumeServerTasks(universe.getNodes())
            .setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);
      }

      Set<NodeDetails> tserverNodes = new HashSet<>(universe.getTServers());
      Set<NodeDetails> masterNodes = new HashSet<>(universe.getMasters());

      createStartMasterTasks(masterNodes)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      createWaitForServersTasks(masterNodes, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      if (EncryptionAtRestUtil.getNumKeyRotations(universe.universeUUID) > 0) {
        createSetActiveUniverseKeysTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      for (NodeDetails node : tserverNodes) {
        createTServerTaskForNode(node, "start")
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      }
      createWaitForServersTasks(tserverNodes, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      createSwamperTargetUpdateTask(false);
      // Create alert definition files.
      createUnivManageAlertDefinitionsTask(true)
          .setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);
      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();

      saveUniverseDetails(
          u -> {
            UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
            universeDetails.universePaused = false;
            u.setUniverseDetails(universeDetails);
          });

      metricService.markSourceActive(params().customerUUID, params().universeUUID);
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
