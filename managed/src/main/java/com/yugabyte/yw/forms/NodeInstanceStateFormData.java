// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.NodeInstance.State;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;

public class NodeInstanceStateFormData {

  @Constraints.Required
  @ApiModelProperty(value = "Target state of the node instance")
  public State state;
}
