// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class ReleaseFormData {
  @Constraints.Required() public String version;
}
