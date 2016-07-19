// Copyright (c) YugaByte, Inc.

package forms.commissioner;

import java.util.UUID;

import controllers.commissioner.Common.CloudType;

public class TaskParamsBase implements ITaskParams {
  // The cloud provider to get node details.
  public CloudType cloud;
  // The node about which we need to fetch details.
  public String nodeName;
  // The instance against which this node's details should be saved.
  public UUID universeUUID;
}
