// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks.local;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.net.HostAndPort;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.tasks.AnsibleUpdateNodeInfo;
import models.commissioner.InstanceInfo;
import controllers.commissioner.tasks.InstanceTaskBase;
import controllers.commissioner.tasks.AnsibleUpdateNodeInfo.Params;

import org.yb.client.MiniYBCluster;
import util.Util;

public class LocalAnsibleUpdateNodeInfo extends AnsibleUpdateNodeInfo {
  public static final Logger LOG = LoggerFactory.getLogger(AnsibleUpdateNodeInfo.class);
  @Override
  public void run() {
    Params params = (Params)taskParams;
    try {
      List<HostAndPort> miniHostPorts = InstanceTaskBase.miniCluster.getMasterHostPorts();
      // Get the node details from the mini-cluster. Relies on nodeInstanceName ending 
      // in a digit, upto 10 nodes. TODO: Check all the info needed in node details is filled up.
      int idx = Character.getNumericValue(taskParams.nodeInstanceName.charAt(
          taskParams.nodeInstanceName.length()-1)) - 1;
      HostAndPort hp = params.isCreateInstance ? miniHostPorts.get(idx)
                                               : InstanceTaskBase.miniCluster.startShellMaster();

      InstanceInfo.NodeDetails nodeDetails = new InstanceInfo.NodeDetails();
      nodeDetails.public_ip = hp.getHostText();
      nodeDetails.masterRpcPort = hp.getPort();
      nodeDetails.az = idx + "a";
      nodeDetails.cloud = "aws";
      nodeDetails.region = "oregon-west";
      nodeDetails.subnet_id = taskParams._local_test_subnets.get(idx);
      nodeDetails.isBeingSetup = !params.isCreateInstance;
      nodeDetails.instance_name = taskParams.nodeInstanceName;
      // Save the node details, either as a created node or a being added one to new details.
      if (params.isCreateInstance) {
        InstanceInfo.updateNodeDetails(taskParams.instanceUUID,
                                       taskParams.nodeInstanceName,
                                       nodeDetails);
      } else {
        InstanceInfo.updateEditNodeDetails(taskParams.instanceUUID,
                                           taskParams.nodeInstanceName,
                                           nodeDetails);
      }
      LOG.info("Local Testing mode: uuid={} - adding {}:{} from {}, sizes={}/{}.",
               taskParams.nodeInstanceName, hp.getHostText(), hp.getPort(), idx,
               InstanceInfo.getNodeDetails(taskParams.instanceUUID).size(), !params.isCreateInstance ? 
                 InstanceInfo.getEditNodeDetails(taskParams.instanceUUID).size() : 0);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
