package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import play.data.validation.Constraints.MaxLength;
import play.data.validation.Constraints.Required;

@ApiModel(description = "xcluster create form")
public class XClusterConfigCreateFormData {

  @Required
  @MaxLength(256)
  @ApiModelProperty(value = "Name", required = true)
  public String name;

  @Required
  @ApiModelProperty(value = "Source Universe UUID", required = true)
  public UUID sourceUniverseUUID;

  @Required
  @ApiModelProperty(value = "Target Universe UUID", required = true)
  public UUID targetUniverseUUID;

  @Required
  @ApiModelProperty(value = "Source Universe table IDs", required = true)
  public Set<String> tables;
}
