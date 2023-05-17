// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.NodeAgent;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
@ApiModel(description = "Node agent details")
public class NodeAgentResp {
  @JsonUnwrapped private final NodeAgent nodeAgent;

  @ApiModelProperty(accessMode = READ_ONLY)
  private boolean versionMatched;

  @ApiModelProperty(accessMode = READ_ONLY)
  private boolean reachable;

  public NodeAgentResp(NodeAgent nodeAgent) {
    this.nodeAgent = nodeAgent;
  }
}
