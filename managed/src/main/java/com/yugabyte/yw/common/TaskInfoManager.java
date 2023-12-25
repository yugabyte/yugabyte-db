// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.TaskInfo;
import javax.inject.Singleton;
import org.apache.commons.collections4.CollectionUtils;
import java.util.List;
import java.util.UUID;

@Singleton
public class TaskInfoManager {
  public boolean isDuplicateDeleteBackupTask(UUID customerUUID, UUID backupUUID) {
    List<TaskInfo> duplicateTasks =
        TaskInfo.findDuplicateDeleteBackupTasks(customerUUID, backupUUID);
    if (duplicateTasks != null && !duplicateTasks.isEmpty()) {
      return true;
    }
    return false;
  }

  public boolean isDeleteBackupTaskAlreadyPresent(UUID customerUUID, UUID backupUUID) {
    List<TaskInfo> tasksList = TaskInfo.findIncompleteDeleteBackupTasks(customerUUID, backupUUID);
    return !CollectionUtils.isEmpty(tasksList);
  }
}
