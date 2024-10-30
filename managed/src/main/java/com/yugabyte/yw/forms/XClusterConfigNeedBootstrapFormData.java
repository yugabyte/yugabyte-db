package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import lombok.ToString;
import play.data.validation.Constraints.Required;

@ToString()
public class XClusterConfigNeedBootstrapFormData {

  @Required
  @ApiModelProperty(
      value = "Source universe table IDs to check whether they need bootstrap",
      example = "[\"000033df00003000800000000000400b\"]",
      required = true)
  public Set<String> tables;

  @ApiModelProperty(
      value =
          "If specified and tables do not exist on the target universe, bootstrapping is required.")
  public UUID targetUniverseUUID;
}
