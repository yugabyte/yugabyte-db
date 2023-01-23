// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for
 * PlacementRegion Data
 */
public class AvailabilityZoneFormData {

  @Constraints.Required()
  @ApiModelProperty(value = "List of availability zones", required = true)
  public List<AvailabilityZoneData> availabilityZones;
}
