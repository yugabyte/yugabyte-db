// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import java.util.HashMap;
import java.util.Map;

/**
 * Details of a universe.
 */
public class UniverseDetails {
  // The configuration intent as specified by the user.
  public UserIntent userIntent;

  // The prefix to be used to generate node names.
  public String nodePrefix;

  // Number of nodes in the universe.
  public int numNodes;

  // The software package to install.
  public String ybServerPkg;

  // The placement info computed from the user request.
  public PlacementInfo placementInfo;

  // All the nodes in the cluster along with their properties.
  public Map<String, NodeDetails> nodeDetailsMap;

  // Set to true when an edit intent on the universe is started.
  public boolean updateInProgress = false;

  // This tracks the if latest edit on this universe has successfully completed. This flag is
  // reset each time an update operation on the universe starts, and is set at the very end of the
  // update operation.
  public boolean updateSucceeded = true;

  public UniverseDetails() {
    nodeDetailsMap = new HashMap<String, NodeDetails>();
  }
}
