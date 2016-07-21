// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.forms;

import java.util.UUID;

import com.yugabyte.yw.commissioner.controllers.Common.CloudType;

public class TaskParamsBase implements ITaskParams {
  // The cloud provider to get node details.
  public CloudType cloud;
  // The node about which we need to fetch details.
  public String nodeName;
  // The instance against which this node's details should be saved.
  public UUID universeUUID;
}
