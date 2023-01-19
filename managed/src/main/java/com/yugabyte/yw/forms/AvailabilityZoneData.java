package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;

@ApiModel(
    description =
        "Details of an availability zone, used by the API and UI to validate data"
            + " against input constraints")
public class AvailabilityZoneData extends AvailabilityZoneEditData {

  @Constraints.Required()
  @ApiModelProperty(value = "AZ name", required = true)
  public String name;

  @Constraints.Required()
  @ApiModelProperty(value = "AZ code", required = true)
  public String code;
}
