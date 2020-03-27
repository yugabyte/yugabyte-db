// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.Date;
import java.util.UUID;

public class CustomerTaskFormData {
  public UUID id;

  public String title;

  public int percentComplete;

  public Date createTime;

  public Date completionTime;

  public String target;

  public UUID targetUUID;

  public String type;

  public String status;
}
