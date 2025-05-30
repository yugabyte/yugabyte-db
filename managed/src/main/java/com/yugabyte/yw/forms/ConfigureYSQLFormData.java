// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import play.data.validation.Constraints;

@ApiModel(value = "ConfigureYSQLFormData", description = "YSQL properties")
public class ConfigureYSQLFormData {

  @ApiModelProperty(value = "Enable YSQL Api for the universe")
  @Constraints.Required
  public boolean enableYSQL;

  @ApiModelProperty(value = "Enable Connection Pooling for the universe")
  public boolean enableConnectionPooling = false;

  @ApiModelProperty(
      value =
          "YbaApi Internal. Extra Connection Pooling gflags for the universe. Only Supported for"
              + " VMs and not yet k8s.")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.2.1.0")
  public Map<UUID, SpecificGFlags> connectionPoolingGflags = new HashMap<>();

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
    params.connectionPoolingGflags = this.connectionPoolingGflags;
    params.enableYSQLAuth = this.enableYSQLAuth;
    params.ysqlPassword = this.ysqlPassword;
    params.communicationPorts.ysqlServerHttpPort = this.communicationPorts.ysqlServerHttpPort;
    params.communicationPorts.ysqlServerRpcPort = this.communicationPorts.ysqlServerRpcPort;
    params.communicationPorts.internalYsqlServerRpcPort =
        this.communicationPorts.internalYsqlServerRpcPort;
    params.configureServer = UniverseTaskBase.ServerType.YSQLSERVER;
  }
}
