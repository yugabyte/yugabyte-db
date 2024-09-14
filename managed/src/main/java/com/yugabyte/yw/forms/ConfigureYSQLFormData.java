// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;

@ApiModel(value = "ConfigureYSQLFormData", description = "YSQL properties")
public class ConfigureYSQLFormData {

  @ApiModelProperty(value = "Enable YSQL Api for the universe")
  @Constraints.Required
  public boolean enableYSQL;

  @ApiModelProperty(value = "Enable Connection Pooling for the universe")
  public boolean enableConnectionPooling = false;

  @ApiModelProperty(value = "Enable YSQL Auth for the universe")
  @Constraints.Required
  public boolean enableYSQLAuth;

  @ApiModelProperty(value = "YSQL Auth password")
  public String ysqlPassword;

  @ApiModelProperty(value = "Communication ports for the universe")
  public UniverseTaskParams.CommunicationPorts communicationPorts =
      new UniverseTaskParams.CommunicationPorts();

  @JsonIgnore
  public void mergeWithConfigureDBApiParams(ConfigureDBApiParams params) {
    params.enableYSQL = this.enableYSQL;
    params.enableConnectionPooling = this.enableConnectionPooling;
    params.enableYSQLAuth = this.enableYSQLAuth;
    params.ysqlPassword = this.ysqlPassword;
    params.communicationPorts.ysqlServerHttpPort = this.communicationPorts.ysqlServerHttpPort;
    params.communicationPorts.ysqlServerRpcPort = this.communicationPorts.ysqlServerRpcPort;
    params.communicationPorts.internalYsqlServerRpcPort =
        this.communicationPorts.internalYsqlServerRpcPort;
    params.configureServer = UniverseTaskBase.ServerType.YSQLSERVER;
  }
}
