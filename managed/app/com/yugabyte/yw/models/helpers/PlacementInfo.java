// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;


import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/**
 * The placement info is a tree. The first level contains a list of clouds. Every cloud contains a
 * list of regions. Each region has a list of AZs. The number of leaves in this tree should be
 * equal to the replication factor, and each leaf defines the data placement (by virtue of its
 * path from the first level).
 */
public class PlacementInfo {
  public static class PlacementCloud {
    // The cloud provider id.
    public UUID uuid;
    // The cloud provider name.
    public String name;
    // The list of region in this cloud we want to place data in.
    public List<PlacementRegion> regionList = new ArrayList<PlacementRegion>();
  }

  public static class PlacementRegion {
    // The region provider id.
    public UUID uuid;
    // The actual provider given region code.
    public String code;
    // The region name.
    public String name;
    // The list of AZs inside this region into which we want to place data.
    public List<PlacementAZ> azList = new ArrayList<PlacementAZ>();
  }

  public static class PlacementAZ {
    // The AZ provider id.
    public UUID uuid;
    // The AZ name.
    public String name;
    // The number of copies of data we should place into this AZ.
    public int replicationFactor;
    // The subnet in the AZ.
    public String subnet;
  }

  // The list of clouds to place data in.
  public List<PlacementCloud> cloudList = new ArrayList<PlacementCloud>();
}
