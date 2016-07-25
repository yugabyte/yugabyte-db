// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metamaster;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.NodeDetails;

import play.data.FormFactory;
import play.mvc.Controller;
import play.mvc.Result;

public class MetaMasterController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(MetaMasterController.class);

  @Inject
  FormFactory formFactory;

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
    } catch (IllegalStateException e) {
      // If the entry was not found, return an error.
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
    // Type of the node (example: c3.xlarge).
    public String instance_type;
    // The private ip address
    public String private_ip;
    // The public ip address.
    public String public_ip;
    // The public dns name of the node.
    public String public_dns;
    // The private dns name of the node.
    public String private_dns;
    // AWS only. The id of the subnet into which this node is deployed.
    public String subnet_id;
    // The az into which the node is deployed.
    public String az;
    // The region into which the node is deployed.
    public String region;
    // The cloud provider where the node is located.
    public String cloud;
    // The master rpc port.
    public int masterRpcPort;

    public static MasterNode fromUniverseNode(NodeDetails uNode) {
      MasterNode mNode = new MasterNode();
      mNode.instance_type = uNode.instance_type;
      mNode.private_ip = uNode.private_ip;
      mNode.public_ip = uNode.public_ip;
      mNode.public_dns = uNode.public_dns;
      mNode.private_dns = uNode.private_dns;
      mNode.subnet_id = uNode.subnet_id;
      mNode.az = uNode.az;
      mNode.region = uNode.region;
      mNode.cloud = uNode.cloud;
      mNode.masterRpcPort = uNode.masterRpcPort;

      return mNode;
    }
  }
}
