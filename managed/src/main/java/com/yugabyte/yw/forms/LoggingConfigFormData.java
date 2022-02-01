// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class LoggingConfigFormData {
  @Constraints.Required() public String level;

  public String getLevel() {
    return level;
  }
}
