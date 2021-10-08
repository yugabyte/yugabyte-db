package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;

@ApiModel(description = "xcluster create form")
public class XClusterConfigCreateFormData {

  @ApiModelProperty(value = "Name", required = true)
  public String name;

  @ApiModelProperty(value = "Source Universe UUID", required = true)
  public UUID sourceUniverseUUID;

  @ApiModelProperty(value = "Target Universe UUID", required = true)
  public UUID targetUniverseUUID;

  @ApiModelProperty(value = "Source Universe table IDs", required = true)
  public Set<String> tables;

  @ApiModelProperty(value = "Bootstrap IDs")
  public Set<String> bootstrapIds;
}
