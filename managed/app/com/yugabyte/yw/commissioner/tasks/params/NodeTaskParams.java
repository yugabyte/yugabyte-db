// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.commissioner.Common.CloudType;

public class NodeTaskParams extends UniverseTaskParams {
  // The cloud provider to get node details.
  public CloudType cloud;

  // The node about which we need to fetch details.
  public String nodeName;
}
