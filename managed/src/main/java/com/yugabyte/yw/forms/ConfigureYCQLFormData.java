// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;

@ApiModel(value = "ConfigureYCQLFormData", description = "YCQL properties")
public class ConfigureYCQLFormData {

  @ApiModelProperty(value = "Enable YCQL Api for the universe")
  @Constraints.Required
  public boolean enableYCQL;

  @ApiModelProperty(value = "Enable YCQL Auth for the universe")
  @Constraints.Required
  public boolean enableYCQLAuth;

  @ApiModelProperty(value = "YCQL Auth password")
  public String ycqlPassword;

  @ApiModelProperty(value = "Communication ports for the universe")
  public UniverseTaskParams.CommunicationPorts communicationPorts =
      new UniverseTaskParams.CommunicationPorts();

  @JsonIgnore
  public void mergeWithConfigureDBApiParams(ConfigureDBApiParams params) {
    params.enableYCQL = this.enableYCQL;
    params.enableYCQLAuth = this.enableYCQLAuth;
    params.ycqlPassword = this.ycqlPassword;
    params.communicationPorts.yqlServerRpcPort = this.communicationPorts.yqlServerRpcPort;
    params.communicationPorts.yqlServerHttpPort = this.communicationPorts.yqlServerHttpPort;
    params.configureServer = UniverseTaskBase.ServerType.YQLSERVER;
  }
}
