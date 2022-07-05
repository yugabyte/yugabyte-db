package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.XClusterConfig;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints.Required;

@ApiModel(description = "xcluster get response")
public class XClusterConfigGetResp {

  //  @JsonUnwrapped public XClusterConfig xClusterConfig;
  @JsonUnwrapped public XClusterConfig xclusterConfig;

  @Required
  @ApiModelProperty(value = "Lag metric data", required = true)
  // TODO: Define and use a concrete type for metrics responses
  public Object lag;
}
