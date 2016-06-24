// Copyright (c) Yugabyte, Inc.

package models.commissioner;

import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.Transactional;
import com.fasterxml.jackson.databind.JsonNode;

import play.data.validation.Constraints;
import play.libs.Json;

@Entity
public class InstanceInfo extends Model {
  // The instance UUID.
  @Id
  private UUID instanceUUID;

  @Constraints.Required
  @Column(nullable = false)
  @DbJson
  private JsonNode instanceDetails;

  public static final Find<UUID, InstanceInfo> find = new Find<UUID, InstanceInfo>(){};

  @Transactional
  public static void upsertInstance(UUID instanceUUID,
                                    List<String> subnets,
                                    int numNodes,
                                    String ybServerPkg) {
    // Find the instance. Update the instance if it exists.
    InstanceInfo instanceInfo = find.byId(instanceUUID);
    if (instanceInfo != null) {
      updateInstanceDetails(instanceUUID, subnets, numNodes, ybServerPkg);
      return;
    }

    // The object does not exist, so create it.
    instanceInfo = new InstanceInfo();

    // Create the instance details.
    InstanceDetails details = new InstanceDetails();
    details.subnets = subnets;
    details.numNodes = numNodes;
    details.ybServerPkg = ybServerPkg;

    // Update the instance info object.
    instanceInfo.instanceUUID = instanceUUID;
    instanceInfo.setDetails(details);

    // Save the object.
    instanceInfo.save();
  }

  // Helper method to update the instance details.
  @Transactional
  public static void updateInstanceDetails(UUID instanceUUID,
                                          List<String> subnets,
                                          int numNodes) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Make the desired updates.
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    details.subnets = subnets;
    details.numNodes = numNodes;
    instanceInfo.setDetails(details);

    // Save the instance back.
    instanceInfo.save();
  }

  // Helper method to update the instance details.
  @Transactional
  public static void updateInstanceDetails(UUID instanceUUID,
                                          List<String> subnets,
                                          int numNodes,
                                          String ybServerPkg) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Make the desired updates.
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    details.subnets = subnets;
    details.numNodes = numNodes;
    details.ybServerPkg = ybServerPkg;
    instanceInfo.setDetails(details);

    // Save the instance back.
    instanceInfo.save();
  }

  // Helper method to update the Node details.
  @Transactional
  public static void updateNodeDetails(UUID instanceUUID,
                                       String nodeName,
                                       NodeDetails nodeDetails) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Make the desired updates.
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    nodeDetails.instance_name = nodeName;
    details.nodeDetailsMap.put(nodeName, nodeDetails);
    instanceInfo.setDetails(details);

    // Save the instance back.
    instanceInfo.save();
  }

  // Helper method to set/unset a node as a master.
  @Transactional
  public static void updateNodeDetails(UUID instanceUUID,
                                       String nodeName,
                                       boolean isMaster) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Make the desired updates.
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    details.nodeDetailsMap.get(nodeName).isMaster = isMaster;
    instanceInfo.setDetails(details);

    // Save the instance back.
    instanceInfo.save();
  }

  public static InstanceDetails getDetails(UUID instanceUUID) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Return the instance details object.
    return Json.fromJson(instanceInfo.instanceDetails, InstanceInfo.InstanceDetails.class);
  }

  /**
   * Return the list of masters for this instance.
   * @param instanceUUID
   * @return
   */
  public static List<NodeDetails> getMasters(UUID instanceUUID) {
    List<NodeDetails> masters = new LinkedList<NodeDetails>();
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    for (NodeDetails nodeDetails : details.nodeDetailsMap.values()) {
      if (nodeDetails.isMaster) {
        masters.add(nodeDetails);
      }
    }
    return masters;
  }

  /**
   * Return the list of tservers for this instance.
   * @param instanceUUID
   * @return
   */
  public static List<NodeDetails> getTServers(UUID instanceUUID) {
    List<NodeDetails> tservers = new LinkedList<NodeDetails>();
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    for (NodeDetails nodeDetails : details.nodeDetailsMap.values()) {
      if (nodeDetails.isTserver) {
        tservers.add(nodeDetails);
      }
    }
    return tservers;
  }

  /**
   * Returns a comma separated list of <privateIp:masterRpcPort> for all nodes that have the
   * isMaster flag set to true in this cluster.
   * @param instanceUUID
   * @return
   */
  public static String getMasterAddresses(UUID instanceUUID) {
    List<NodeDetails> masters = getMasters(instanceUUID);
    StringBuilder masterAddresses = new StringBuilder();
    for (NodeDetails nodeDetails : masters) {
      if (nodeDetails.isMaster) {
        if (masterAddresses.length() != 0) {
          masterAddresses.append(",");
        }
        masterAddresses.append(nodeDetails.private_ip);
        masterAddresses.append(":");
        masterAddresses.append(nodeDetails.masterRpcPort);
      }
    }
    return masterAddresses.toString();
  }

  private void setDetails(InstanceDetails details) {
    this.instanceDetails = Json.toJson(details);
  }

  public static class InstanceDetails {
    // Subnets the instance nodes should be deployed into.
    public List<String> subnets;

    // Number of nodes in the instance.
    public int numNodes;

    // The software package to install.
    public String ybServerPkg;

    // All the nodes in the cluster along with their properties.
    public Map<String, NodeDetails> nodeDetailsMap;

    public InstanceDetails() {
      subnets = new LinkedList<String>();
      nodeDetailsMap = new HashMap<String, NodeDetails>();
    }
  }

  /**
   * Represents all the details of a cloud node that are of interest.
   *
   * NOTE: the names of fields in this class MUST correspond to the output field names of the script
   * 'find_host.sh' which is in the 'devops' repository.
   */
  public static class NodeDetails {
    public String instance_name;
    public String instance_type;
    public String private_ip;
    public String public_ip;
    public String public_dns;
    public String private_dns;
    public String subnet_id;
    public String az;
    public String region;
    public String cloud;

    public boolean isMaster;
    public int masterHttpPort = 7000;
    public int masterRpcPort = 7100;

    public boolean isTserver;
    public int tserverHttpPort = 9000;
    public int tserverRpcPort = 9100;
  }
}
