// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.NodeAgent;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
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

  @ApiModelProperty(accessMode = READ_ONLY)
  private String providerName;

  @ApiModelProperty(accessMode = READ_ONLY)
  private String universeName;

  @ApiModelProperty(accessMode = READ_ONLY)
  private UUID providerUuid;

  @ApiModelProperty(accessMode = READ_ONLY)
  private UUID universeUuid;

  public NodeAgentResp(NodeAgent nodeAgent) {
    this.nodeAgent = nodeAgent;
  }
}
