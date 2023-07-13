/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.helpers.provider.region;

import com.fasterxml.jackson.annotation.JsonAlias;
import java.util.List;
import java.util.Map;
import lombok.Data;

@Data
public class RegionMetadata {
  private Map<String, RegionMetadataInfo> regionMetadata;

  @Data
  public static class RegionMetadataInfo {
    private String name;
    private double latitude;
    private double longitude;

    @JsonAlias("availabilty_zones")
    private List<String> availabilityZones;
  }
}
