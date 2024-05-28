// Copyright (c) YugaByte, Inc.

package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.Data;

@Data
public class PlacementInfo {

  @Data
  public static class PlacementCloud {
    private UUID uuid;
    private String code;
    private UUID defaultRegion;
    private List<PlacementRegion> regionList = new ArrayList<>();
  }

  @Data
  public static class PlacementRegion {
    private UUID uuid;
    private String code;
    private String name;
    private List<PlacementAZ> azList = new ArrayList<PlacementAZ>();
  }

  @Data
  public static class PlacementAZ {
    private UUID uuid;
    private String name;
    private int replicationFactor;
    private String subnet;
    private String secondarySubnet;
    private int numNodesInAZ;

    @JsonProperty("isAffinitized")
    private boolean isAffinitized;
  }

  private List<PlacementCloud> cloudList = new ArrayList<PlacementCloud>();
}
