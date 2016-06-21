// Copyright (c) YugaByte, Inc.

package forms.commissioner;

import java.util.List;
import java.util.UUID;

import controllers.commissioner.Common.CloudType;
import org.yb.client.MiniYBCluster;

public class TaskParamsBase implements ITaskParams {
  // The cloud provider to get node details.
  public CloudType cloud;
  // The node about which we need to fetch details.
  public String nodeInstanceName;
  // The instance against which this node's details should be saved.
  public UUID instanceUUID;

  // Used by local cluster testing via ansible emulation.
  // TODO: See if we can move it out of this base class.
  public List<String> _local_test_subnets;
}
