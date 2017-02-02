// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Customer;
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
    return getServerAddresses(customerUUID, universeUUID, ServerType.MASTER);
  }

  public Result getYQLServerAddresses(UUID customerUUID, UUID universeUUID) {
    return getServerAddresses(customerUUID, universeUUID, ServerType.YQLSERVER);
  }

  public Result getRedisServerAddresses(UUID customerUUID, UUID universeUUID) {
    return getServerAddresses(customerUUID, universeUUID, ServerType.REDISSERVER);
  }

  private Result getServerAddresses(UUID customerUUID, UUID universeUUID, ServerType type) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    try {
      // Lookup the entry for the instanceUUID.
      Universe universe = Universe.get(universeUUID);
      switch (type) {
        case MASTER: return ApiResponse.success(universe.getMasterAddresses());
        case YQLSERVER: return ApiResponse.success(universe.getYQLServerAddresses());
        case REDISSERVER: return ApiResponse.success(universe.getRedisServerAddresses()); 
        default: throw new IllegalArgumentException("Unexpected type " + type);
      }
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
