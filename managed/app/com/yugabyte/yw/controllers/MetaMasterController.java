// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.mvc.Controller;
import play.mvc.Result;

public class MetaMasterController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(MetaMasterController.class);

  public Result get(UUID universeUUID) {
    try {
      // Lookup the entry for the instanceUUID.
      Universe universe = Universe.get(universeUUID);
      // Return the result.
      MastersList masters = new MastersList();
      for (NodeDetails node : universe.getMasters()) {
        masters.add(MasterNode.fromUniverseNode(node));
      }
      return ApiResponse.success(masters);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "Could not find universe " + universeUUID);
    }
  }

  public Result getMasterAddresses(UUID customerUUID, UUID universeUUID) {
    // TODO: verify customer has the universe.
    try {
      // Lookup the entry for the instanceUUID.
      Universe universe = Universe.get(universeUUID);
      return ApiResponse.success(universe.getMasterAddresses());
    } catch (RuntimeException e) {
      LOG.error("Error finding universe", e);
      return ApiResponse.error(BAD_REQUEST, "Could not find universe " + universeUUID);
    }
  }

  public static class MastersList {
    public List<MasterNode> masters = new ArrayList<MasterNode>();

    public void add(MasterNode masterNode) {
      masters.add(masterNode);
    }
  }

  public static class MasterNode {
    // Information about the node that is returned by the cloud provider.
    public CloudSpecificInfo cloudInfo;

    // The master rpc port.
    public int masterRpcPort;

    public static MasterNode fromUniverseNode(NodeDetails uNode) {
      MasterNode mNode = new MasterNode();
      mNode.cloudInfo = uNode.cloudInfo;
      mNode.masterRpcPort = uNode.masterRpcPort;

      return mNode;
    }
  }
}
