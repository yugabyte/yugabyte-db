// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTableSpaces;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CreateTableSpacesInUniverse extends UniverseTaskBase {

  @Inject
  protected CreateTableSpacesInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected CreateTableSpaces.Params taskParams() {
    return (CreateTableSpaces.Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    if (!primaryCluster.userIntent.enableYSQL) {
      throw new PlatformServiceException(
          BAD_REQUEST, "The universe doesn't have YSQL enabled. Unable to proceed the request.");
    }

    try {
      checkUniverseVersion();

      // Update the DB to prevent other changes from happening.
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);

      createTableSpacesTask().setSubTaskGroupType(SubTaskGroupType.CreatingTablespaces);

      // Marks the update of this universe as a success only if all the tasks before
      // it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.CreatingTablespaces);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future updates to
      // the universe.
      unlockUniverseForUpdate();
    }
  }

  private SubTaskGroup createTableSpacesTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateTablespaces");
    CreateTableSpaces task = createTask(CreateTableSpaces.class);
    CreateTableSpaces.Params params = taskParams();
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
