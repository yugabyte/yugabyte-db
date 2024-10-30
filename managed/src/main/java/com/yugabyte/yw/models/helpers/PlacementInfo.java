// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * The placement info is a tree. The first level contains a list of clouds. Every cloud contains a
 * list of regions. Each region has a list of AZs. The number of leaves in this tree should be equal
 * to the replication factor, and each leaf defines the data placement (by virtue of its path from
 * the first level).
 */
public class PlacementInfo {
  public static class PlacementCloud {
    // The cloud provider id.
    @ApiModelProperty public UUID uuid;
    // The cloud provider code.
    @ApiModelProperty public String code;
    // The list of region in this cloud we want to place data in.
    @ApiModelProperty public List<PlacementRegion> regionList = new ArrayList<>();
    // UUID of default region. For universes with more AZs than RF, the default
    // placement for user tables will be RF AZs in the default region. This is
    // commonly encountered in geo-partitioning use cases.
    @ApiModelProperty public UUID defaultRegion;

    @Override
    public String toString() {
      StringBuilder ret = new StringBuilder("Cloud=").append(code).append(" ");
      ret.append(
          String.join(
              ", ",
              regionList.stream().map(PlacementRegion::toString).collect(Collectors.toList())));
      ret.append("; Default region=").append(defaultRegion);
      return ret.toString();
    }
  }

  public static class PlacementRegion {
    // The region provider id.
    @ApiModelProperty public UUID uuid;
    // The actual provider given region code.
    @ApiModelProperty public String code;
    // The region name.
    @ApiModelProperty public String name;
    // The list of AZs inside this region into which we want to place data.
    @ApiModelProperty public List<PlacementAZ> azList = new ArrayList<PlacementAZ>();
    // The Load Balancer FQDN.
    @ApiModelProperty public String lbFQDN;

    @Override
    public String toString() {
      String ret = "Region=" + code + " : ";
      ret +=
          String.join(
              ", ", azList.stream().map(PlacementAZ::toString).collect(Collectors.toList()));
      return ret;
    }
  }

  @JsonIgnoreProperties(
      // Ignore auto-generated boolean properties: https://stackoverflow.com/questions/32270422
      value = {"affinitized"},
      ignoreUnknown = true)
  public static class PlacementAZ {
    // The AZ provider id.
    @ApiModelProperty public UUID uuid;
    // The AZ name.
    @ApiModelProperty public String name;
    // The minimum number of copies of data we should place into this AZ.
    @ApiModelProperty public int replicationFactor;
    // The subnet in the AZ.
    @ApiModelProperty public String subnet;
    // The secondary subnet in the AZ.
    @ApiModelProperty public String secondarySubnet;
    // Number of nodes in each Az.
    @ApiModelProperty public int numNodesInAZ;
    // Is this an affinitized zone.
    @ApiModelProperty public boolean isAffinitized;
    // The Load Balancer id.
    @ApiModelProperty public String lbName;

    @Override
    public String toString() {
      return "(AZ="
          + name
          + ", count="
          + numNodesInAZ
          + ", replication factor="
          + replicationFactor
          + ")";
    }
  }

  // The list of clouds to place data in.
  public List<PlacementCloud> cloudList = new ArrayList<PlacementCloud>();

  @JsonIgnore
  public Stream<PlacementAZ> azStream() {
    return cloudList.stream()
        .flatMap(cloud -> cloud.regionList.stream())
        .flatMap(region -> region.azList.stream());
  }

  @JsonIgnore
  public PlacementAZ findByAZUUID(UUID azUUID) {
    return azStream().filter(az -> Objects.equals(azUUID, az.uuid)).findFirst().orElse(null);
  }

  @Override
  public String toString() {
    String ret = "";
    for (PlacementCloud cloud : cloudList) {
      ret += cloud;
    }
    return ret;
  }
}
