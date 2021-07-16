// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for
 * PlacementRegion Data
 */
public class AvailabilityZoneFormData {

  @Constraints.Required()
  @ApiModelProperty(value = "Availability zones", required = true)
  public List<AvailabilityZoneData> availabilityZones;

  @ApiModel(value = "Availability Zones", description = "Availability Zones.")
  public static class AvailabilityZoneData {
    @Constraints.Required()
    @ApiModelProperty(value = "Az code", required = true)
    public String code;

    @Constraints.Required()
    @ApiModelProperty(value = "Az name", required = true)
    public String name;

    @ApiModelProperty(value = "Az subnet")
    public String subnet;
  }
}
