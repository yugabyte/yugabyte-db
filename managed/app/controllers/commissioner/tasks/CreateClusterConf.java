package controllers.commissioner.tasks;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.TreeSet;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.AbstractTaskBase;
import models.commissioner.InstanceInfo;
import models.commissioner.InstanceInfo.InstanceDetails;
import models.commissioner.InstanceInfo.NodeDetails;

public class CreateClusterConf extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateClusterConf.class);

  public static final int numMastersToChoose = 3;

  // This is the maximum number of subnets that the masters can be placed across, and need to be an
  // odd number for consensus to work.
  public static final int maxMasterSubnets = 3;

  public static class Params extends AbstractTaskBase.TaskParamsBase {}

  public CreateClusterConf(Params params) {
    super(params);
  }

  @Override
  public void run() {
    // NOTE: we are performing a read-modify-write without a transaction. This is ok as no one else
    // will be updating the cluster info at this point. This task runs on only one thread.

    // Find the instance. Update the instance if it exists.
    InstanceDetails instanceDetails = InstanceInfo.getDetails(taskParams.instanceUUID);
    if (instanceDetails == null) {
      LOG.error("Instance " + taskParams.instanceUUID + " not found.");
      return;
    }

    // Group the cluster nodes by subnets.
    Map<String, TreeSet<String>> subnetsToNodenameMap = new HashMap<String, TreeSet<String>>();
    for (Entry<String, NodeDetails> entry : instanceDetails.nodeDetailsMap.entrySet()) {
      TreeSet<String> nodeSet = subnetsToNodenameMap.get(entry.getValue().subnet_id);
      // If the node set is empty, create it.
      if (nodeSet == null) {
        nodeSet = new TreeSet<String>();
      }
      // Add the node name into the node set.
      nodeSet.add(entry.getKey());
      // Add the node set back into the map.
      subnetsToNodenameMap.put(entry.getValue().subnet_id, nodeSet);
    }

    // Choose the masters such that we have one master per subnet.
    List<String> masters = new ArrayList<String>();
    if (subnetsToNodenameMap.size() >= maxMasterSubnets) {
      for (Entry<String, TreeSet<String>> entry : subnetsToNodenameMap.entrySet()) {
        // Get one node from each subnet.
        String nodeName = entry.getValue().first();
        masters.add(nodeName);
        if (masters.size() == numMastersToChoose) {
          break;
        }
        LOG.info("Chose node " + nodeName + " as a master from subnet " + entry.getKey());
      }
    } else {
      for (NodeDetails node : instanceDetails.nodeDetailsMap.values()) {
        masters.add(node.instance_name);
        if (masters.size() == numMastersToChoose) {
          break;
        }
        LOG.info("Chose node " + node.instance_name + " as a master from subnet " + node.subnet_id);
      }
    }

    // Persist the desired nodes as the masters.
    for (String nodeName : masters) {
      InstanceInfo.updateNodeDetails(taskParams.instanceUUID, nodeName, true);
    }
  }
}
