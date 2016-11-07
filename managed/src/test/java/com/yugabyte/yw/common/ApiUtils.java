// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class ApiUtils {
  public static Universe.UniverseUpdater mockUniverseUpdater() {
    return new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails.userIntent = new UserIntent();
        // Add a desired number of nodes.
        universeDetails.userIntent.numNodes = 3;
        universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.nodeName = "host-n" + idx;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.cloud = "aws";
          node.cloudInfo.az = "az-" + idx;
          node.cloudInfo.region = "test-region";
          node.cloudInfo.subnet_id = "subnet-" + idx;
          node.cloudInfo.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsSet.add(node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(UserIntent userIntent) {
    return new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails = new UniverseDefinitionTaskParams();
        universeDetails.userIntent = userIntent;
        universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.nodeName = "host-n" + idx;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.cloud = "aws";
          node.cloudInfo.subnet_id = "subnet-" + idx;
          node.cloudInfo.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsSet.add(node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
  }
}
