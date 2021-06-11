// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.ArrayList;
import java.util.Map;
import java.util.List;

public class KubernetesProviderFormData extends CloudProviderFormData {

  // Regions available in the provider.
  public List<RegionData> regionList = new ArrayList<RegionData>();

  public static class RegionData {
    @Constraints.Required() public String code;

    public String name;

    public double latitude = 0.0;
    public double longitude = 0.0;

    // Zones available in the region.
    public List<ZoneData> zoneList = new ArrayList<ZoneData>();

    public Map<String, String> config;

    public static class ZoneData {
      @Constraints.Required() public String code;

      @Constraints.Required() public String name;

      public Map<String, String> config;
    }
  }
}
