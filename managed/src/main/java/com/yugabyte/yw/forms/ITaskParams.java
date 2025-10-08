// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;

/** Marker interface. All task params implement this interface. */
public interface ITaskParams {
  void setErrorString(String errorString);

  String getErrorString();

  void setPreviousTaskUUID(UUID previousTaskUUID);

  UUID getPreviousTaskUUID();

  UUID getTargetUuid(TaskType taskType);
}
