// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.List;
import java.util.UUID;

import play.data.validation.Constraints;

/**
 * This class captures the user intent for creation of the universe. Note some nuances in the way
 * the intent is specified.
 *
 * Single AZ deployments:
 * Exactly one region should be specified in the 'regionList'.
 *
 * Muti-AZ deployments:
 * 1. There is at least one region specified which has a at least 'replicationFactor' number of AZs.
 *
 * 2. There are multiple regions specified, and the sum total of all AZs in those regions is greater
 *    than or equal to 'replicationFactor'. In this case, the preferred region can be specified to
 *    hint which region needs to have a majority of the data copies if applicable, as well as
 *    serving as the primary leader. Note that we do not currently support ability to place leaders
 *    in a preferred region.
 *
 * NOTE #1: The regions can potentially be present in different clouds.
 */
public class CreateUniverseFormData {
  // Nice name for the universe.
  @Constraints.Required()
  public String universeName;

  // The default replication factor in this universe.
  @Constraints.Min(3)
  public int replicationFactor = 3;

  // Determines if this universe is a single or multi AZ deployment.
  @Constraints.Required()
  public Boolean isMultiAZ;

  // The list of regions that the user wants to place data replicas into.
  // TODO: remove regionUUID and replace with regionList.
  @Constraints.Required()
  public UUID regionUUID;
  public List<UUID> regionList;

  // The regions that the user wants to nominate as the preferred region. This makes sense only for
  // a multi-region setup.
  public UUID preferredRegion;
}
