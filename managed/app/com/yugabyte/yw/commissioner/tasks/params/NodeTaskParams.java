// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import java.util.UUID;

import com.yugabyte.yw.commissioner.Common.CloudType;

public class NodeTaskParams implements ITaskParams {
  // The cloud provider to get node details.
  public CloudType cloud;

  // The node about which we need to fetch details.
  public String nodeName;

  // The universe against which this node's details should be saved.
  public UUID universeUUID;
}
