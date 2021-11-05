// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigDelete;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigRename;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetStatus;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSync;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import io.ebean.Ebean;
import lombok.extern.slf4j.Slf4j;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import play.api.Play;

@Slf4j
public abstract class XClusterConfigTaskBase extends UniverseTaskBase {

  public YBClientService ybService;

  private static final int POLL_TIMEOUT_SECONDS = 300;

  protected XClusterConfigTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    if (taskParams().xClusterConfig != null) {
      return String.format(
          "%s(uuid=%s, universe=%s)",
          this.getClass().getSimpleName(),
          taskParams().xClusterConfig.uuid,
          taskParams().universeUUID);
    } else {
      return String.format(
          "%s(universe=%s)", this.getClass().getSimpleName(), taskParams().universeUUID);
    }
  }

  @Override
  protected XClusterConfigTaskParams taskParams() {
    return (XClusterConfigTaskParams) taskParams;
  }

  protected XClusterConfig getXClusterConfig() {
    taskParams().xClusterConfig = XClusterConfig.getOrBadRequest(taskParams().xClusterConfig.uuid);
    return taskParams().xClusterConfig;
  }

  protected XClusterConfig refreshXClusterConfig() {
    taskParams().xClusterConfig.refresh();
    Ebean.refreshMany(taskParams().xClusterConfig, "tables");
    return taskParams().xClusterConfig;
  }

  protected void setXClusterConfigStatus(XClusterConfigStatusType status) {
    taskParams().xClusterConfig.status = status;
    taskParams().xClusterConfig.update();
  }

  protected SubTaskGroup createXClusterConfigSetupTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("XClusterConfigSetup", executor);
    XClusterConfigSetup task = createTask(XClusterConfigSetup.class);
    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigToggleStatusTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("XClusterConfigToggleStatus", executor);
    XClusterConfigSetStatus task = createTask(XClusterConfigSetStatus.class);
    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigModifyTablesTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("XClusterConfigModifyTables", executor);
    XClusterConfigModifyTables task = createTask(XClusterConfigModifyTables.class);
    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigRenameTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("XClusterConfigRename", executor);
    XClusterConfigRename task = createTask(XClusterConfigRename.class);
    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigDeleteTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("XClusterConfigDelete", executor);
    XClusterConfigDelete task = createTask(XClusterConfigDelete.class);
    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigSyncTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("XClusterConfigSync", executor);
    XClusterConfigSync task = createTask(XClusterConfigSync.class);
    task.initialize(taskParams());
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  protected interface IPollForXClusterOperation {
    IsSetupUniverseReplicationDoneResponse poll(String replicationGroupName) throws Exception;
  }

  protected void waitForXClusterOperation(IPollForXClusterOperation p) {
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;

    try {
      IsSetupUniverseReplicationDoneResponse doneResponse = null;
      int numAttempts = 1;
      long startTime = System.currentTimeMillis();
      while (((System.currentTimeMillis() - startTime) / 1000) < POLL_TIMEOUT_SECONDS) {
        if (numAttempts % 10 == 0) {
          log.info(
              "Wait for XClusterConfig({}) operation to complete (attempt {})",
              xClusterConfig.uuid,
              numAttempts);
        }

        doneResponse = p.poll(xClusterConfig.getReplicationGroupName());
        if (doneResponse.isDone()) {
          break;
        }
        if (doneResponse.hasError()) {
          log.warn(
              "Failed to wait for XClusterConfig({}) operation: {}",
              xClusterConfig.uuid,
              doneResponse.getError().toString());
        }

        Thread.sleep(1000);
        numAttempts++;
      }

      if (doneResponse == null) {
        throw new RuntimeException(
            String.format(
                "Never received response waiting for XClusterConfig(%s) operation to complete",
                xClusterConfig.uuid));
      }
      if (!doneResponse.isDone()) {
        throw new RuntimeException(
            String.format(
                "Timed out waiting for XClusterConfig(%s) operation to complete",
                xClusterConfig.uuid));
      }
      if (doneResponse.hasReplicationError()
          && doneResponse.getReplicationError().getCode() != ErrorCode.OK) {
        throw new RuntimeException(
            String.format(
                "XClusterConfig(%s) operation failed: %s",
                xClusterConfig.uuid, doneResponse.getReplicationError().toString()));
      }

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }
}
