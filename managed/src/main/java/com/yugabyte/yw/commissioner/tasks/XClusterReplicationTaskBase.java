package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.AlterXClusterReplicationAddTables;
import com.yugabyte.yw.commissioner.tasks.subtasks.AlterXClusterReplicationChangeMasterAddresses;
import com.yugabyte.yw.commissioner.tasks.subtasks.AlterXClusterReplicationRemoveTables;
import com.yugabyte.yw.commissioner.tasks.subtasks.XClusterReplicationSetActive;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.XClusterReplicationTaskParams;
import play.api.Play;

public abstract class XClusterReplicationTaskBase extends UniverseDefinitionTaskBase {

  public YBClientService ybService;

  protected XClusterReplicationTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  protected XClusterReplicationTaskParams taskParams() {
    return (XClusterReplicationTaskParams) taskParams;
  }

  public SubTaskGroup createXClusterReplicationAddTablesTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AlterXClusterReplicationAddTables", executor);
    AlterXClusterReplicationAddTables task = createTask(AlterXClusterReplicationAddTables.class);

    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createXClusterReplicationRemoveTablesTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AlterXClusterReplicationRemoveTables", executor);
    AlterXClusterReplicationRemoveTables task =
        createTask(AlterXClusterReplicationRemoveTables.class);

    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createXClusterReplicationChangeMasterAddressesTask() {
    SubTaskGroup subTaskGroup =
        new SubTaskGroup("AlterXClusterReplicationChangeMasterAddresses", executor);
    AlterXClusterReplicationChangeMasterAddresses task =
        createTask(AlterXClusterReplicationChangeMasterAddresses.class);

    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createPauseXClusterReplicationTask() {
    return createXClusterReplicationSetActiveTask("PauseXClusterReplication", false);
  }

  public SubTaskGroup createResumeXClusterReplicationTask() {
    return createXClusterReplicationSetActiveTask("ResumeXClusterReplication", true);
  }

  private SubTaskGroup createXClusterReplicationSetActiveTask(
      String subTaskGroupName, boolean active) {
    SubTaskGroup subTaskGroup = new SubTaskGroup(subTaskGroupName, executor);
    XClusterReplicationSetActive task = createTask(XClusterReplicationSetActive.class);

    XClusterReplicationTaskParams params = new XClusterReplicationTaskParams(taskParams());
    params.active = active;

    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }
}
