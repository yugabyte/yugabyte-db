package com.yugabyte.yw.forms;

import java.util.Date;
import java.util.UUID;

public class SubTaskFormData {
  public UUID subTaskUUID;

  public Date creationTime;

  public String subTaskType;

  public String subTaskState;

  public String subTaskGroupType;

  public String errorString;
}
