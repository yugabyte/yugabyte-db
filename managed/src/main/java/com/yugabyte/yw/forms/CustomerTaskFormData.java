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

  public String type;

  // We identify if the task was successful or not.
  // Based on that we provide different color coding on the UI
  public boolean success;
}
