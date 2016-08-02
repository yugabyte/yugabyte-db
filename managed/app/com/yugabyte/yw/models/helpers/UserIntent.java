// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import java.util.List;
import java.util.UUID;

/**
 * The user defined intent for the universe.
 */
public class UserIntent {
	// The replication factor.
	public int replicationFactor;

	// Determines if this universe is a single or multi AZ deployment.
	public Boolean isMultiAZ;

	// The list of regions that the user wants to place data replicas into.
	public List<UUID> regionList;

	// The regions that the user wants to nominate as the preferred region. This makes sense only for
	// a multi-region setup.
	public UUID preferredRegion;
}
