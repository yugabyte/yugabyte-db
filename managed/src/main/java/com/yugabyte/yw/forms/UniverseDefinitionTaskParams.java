// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.List;
import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import play.data.validation.Constraints;

/**
 * This class captures the user intent for creation of the universe. Note some nuances in the way
 * the intent is specified.
 * <p>
 * Single AZ deployments:
 * Exactly one region should be specified in the 'regionList'.
 * <p>
 * Multi-AZ deployments:
 * 1. There is at least one region specified which has a at least 'replicationFactor' number of AZs.
 * <p>
 * 2. There are multiple regions specified, and the sum total of all AZs in those regions is greater
 * than or equal to 'replicationFactor'. In this case, the preferred region can be specified to
 * hint which region needs to have a majority of the data copies if applicable, as well as
 * serving as the primary leader. Note that we do not currently support ability to place leaders
 * in a preferred region.
 * <p>
 * NOTE #1: The regions can potentially be present in different clouds.
 */
public class UniverseDefinitionTaskParams extends UniverseTaskParams {
  // The configuration for the universe the user intended.
  @Constraints.Required()
  public UserIntent userIntent;

  // This should be a globally unique name - it is a combination of the customer id and the universe
  // id. This is used as the prefix of node names in the universe.
  public String nodePrefix = null;

  // The set of nodes that are part of this universe.
  public Set<NodeDetails> nodeDetailsSet = null;

  // The placement information computed from the user intent.
  public PlacementInfo placementInfo;

  // Internal UUID picked for this universe. 
  public UUID universeUUID;

  // TODO: Add a version number to prevent stale updates.
  // Set to true when an create/edit/destroy intent on the universe is started.
  public boolean updateInProgress = false;

  // This tracks the if latest operation on this universe has successfully completed. This flag is
  // reset each time a new operation on the universe starts, and is set at the very end of that
  // operation.
  public boolean updateSucceeded = true;

  /**
   * The user defined intent for the universe.
   */
  public static class UserIntent {
    @Constraints.Required()
    // Nice name for the universe.
    public String universeName;

    // The replication factor.
    @Constraints.Min(3)
    public int replicationFactor = 3;

    // Determines if this universe is a single or multi AZ deployment.
    public Boolean isMultiAZ;

    // The list of regions that the user wants to place data replicas into.
    public List<UUID> regionList;

    // The regions that the user wants to nominate as the preferred region. This makes sense only for
    // a multi-region setup.
    public UUID preferredRegion;

    // Cloud Instance Type that the user wants
    @Constraints.Required()
    public String instanceType;

    // The number of nodes to provision. These include ones for both masters and tservers.
    @Constraints.Min(3)
    public int numNodes;

    // The software version of YB to install.
    // TODO: replace with a nicer string as default.
    public String ybServerPackage =
        "yb-server-0.0.1-SNAPSHOT.c31ccae363d3a94ab5ebc7088212a92729df17ea.tar.gz";

    public String toString() {
      return "UserIntent " + "for universe=" + universeName + ", isMultiAZ=" + isMultiAZ + " type=" +
             instanceType + ", numNodes=" + numNodes;
    }
  }
}
