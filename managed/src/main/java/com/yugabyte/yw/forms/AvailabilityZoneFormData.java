// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.List;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for
 * PlacementRegion Data
 */
public class AvailabilityZoneFormData {

  @Constraints.Required() public List<AvailabilityZoneData> availabilityZones;

  public static class AvailabilityZoneData {
    @Constraints.Required() public String code;

    @Constraints.Required() public String name;

    public String subnet;
  }
}
