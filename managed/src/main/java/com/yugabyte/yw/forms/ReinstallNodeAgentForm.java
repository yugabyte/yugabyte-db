// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModelProperty;
import java.util.Set;

/** This class is used by NodeAgentController to send reinstall payload. */
public class ReinstallNodeAgentForm {
  @ApiModelProperty(value = "Node names")
  public Set<String> nodeNames;
}
