package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;

@ApiModel(description = "xcluster sync form")
public class XClusterConfigSyncFormData {

  @ApiModelProperty(value = "Name")
  public String replicationGroupName;

  @ApiModelProperty(value = "Target Universe UUID")
  public UUID targetUniverseUUID;
}
