package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import play.data.validation.Constraints.Required;

public class XClusterConfigNeedBootstrapFormData {

  @Required
  @ApiModelProperty(
      value = "Source universe table IDs to check whether they need bootstrap",
      example = "[000033df00003000800000000000400b]",
      required = true)
  public Set<String> tables;
}
