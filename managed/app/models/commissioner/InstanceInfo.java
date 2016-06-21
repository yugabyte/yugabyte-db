// Copyright (c) YugaByte, Inc.

package models.commissioner;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.util.Collection;
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
  public static final Logger LOG = LoggerFactory.getLogger(InstanceInfo.class);

  // The instance UUID.
  @Id
  private UUID instanceUUID;

  @Constraints.Required
  @Column(nullable = false)
  @DbJson
  private JsonNode instanceDetails;

  // The following is set only when an edit instance is started.
  @Column(nullable = true)
  @DbJson
  private JsonNode editInstanceDetails;

  public static final Find<UUID, InstanceInfo> find = new Find<UUID, InstanceInfo>(){};

  // TODO: Remove all the synchronized and use the correct transactional package to 
  // get serializable semantics. Or use YJson layer once it's ready!

  @Transactional
  public static synchronized void createInstance(UUID instanceUUID,
                                    List<String> subnets,
                                    int numNodes,
                                    String ybServerPkg) {
    // Find the instance. Error out if the instance exists.
    InstanceInfo instanceInfo = find.byId(instanceUUID);
    if (instanceInfo != null) {
      LOG.warn("Instance UUID {} exists with details: {}.",
               instanceInfo.instanceUUID, instanceInfo.instanceDetails.toString());
      throw new IllegalStateException("Instance " + instanceUUID + " already exists, cannot create.");
    }

    // The object does not exist, so create it.
    instanceInfo = new InstanceInfo();

    // Create the instance details.
    InstanceDetails details = new InstanceDetails();
    details.subnets = subnets;
    details.numNodes = numNodes;
    details.ybServerPkg = ybServerPkg;
    details.isBeingEdited = false;
    details.nextNodeIndex = 1;

    // Update the instance info object.
    instanceInfo.instanceUUID = instanceUUID;
    instanceInfo.setDetails(details);
    instanceInfo.setEditInstanceDetails(null);

    // Save the object.
    instanceInfo.save();
  }

  // Helper method to update the instance details with server package info.
  @Transactional
  private static synchronized void updateInstanceDetails(UUID instanceUUID,
                                            List<String> subnets,
                                            int numNodes,
                                            String ybServerPkg) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Make the desired updates.
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    details.subnets = subnets;
    details.numNodes = numNodes;
    details.nodeDetailsMap.clear();
    details.ybServerPkg = ybServerPkg;
    details.isBeingEdited = false;
    instanceInfo.setDetails(details);
    instanceInfo.setEditInstanceDetails(null);

    // Save the instance back.
    instanceInfo.save();
  }

  // Helper method to update the Node details.
  @Transactional
  public static synchronized void updateEditNodeDetails(UUID instanceUUID,
                                       String nodeName,
                                       NodeDetails nodeDetails) {
    updateNodeDetails(instanceUUID, nodeName, nodeDetails, false);
  }

  @Transactional
  public static synchronized void updateNodeDetails(UUID instanceUUID,
                                       String nodeName,
                                       NodeDetails nodeDetails) {
    updateNodeDetails(instanceUUID, nodeName, nodeDetails, true);
  }
  
  @Transactional
  private static synchronized void updateNodeDetails(UUID instanceUUID,
                                       String nodeName,
                                       NodeDetails nodeDetails,
                                       boolean isCreate) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Make the desired updates.
    if (isCreate) {
      InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
      details.nodeDetailsMap.put(nodeName, nodeDetails);
      LOG.info("Incr node {}", details.nextNodeIndex);
      details.nextNodeIndex++;
      instanceInfo.setDetails(details);
    } else {
      InstanceDetails editDetails = InstanceInfo.getEditDetails(instanceUUID);
      LOG.info("Adding {}/{} to map of size {}.",
               nodeName, nodeDetails.public_ip, editDetails.nodeDetailsMap.size());
      editDetails.nodeDetailsMap.put(nodeName, nodeDetails);
      instanceInfo.setEditInstanceDetails(editDetails);
    }

    // Save the instance back.
    instanceInfo.save();
  }

  // Helper method to clear the Node details.
  @Transactional
  public static synchronized void deleteNodeDetails(UUID instanceUUID) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Clear the node details.
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    details.nodeDetailsMap.clear();
    details.nextNodeIndex = 1;
    details.numNodes = 0;
    instanceInfo.setDetails(details);

    // Save the instance back.
    instanceInfo.save();
  }

  // Helper method to clear the Node details.
  @Transactional
  public static synchronized void deleteEditNodeDetails(UUID instanceUUID) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Clear the edit node details.
    InstanceDetails edetails = InstanceInfo.getEditDetails(instanceUUID);
    if (edetails == null) {
      throw new IllegalStateException("No edit details found to delete.");
    }
    edetails.nodeDetailsMap.clear();
    instanceInfo.setEditInstanceDetails(edetails);

    // Save the instance back.
    instanceInfo.save();
  }

  // If the edit instance succeeds, the old intent becomes the final node details.
  @Transactional
  public static synchronized void switchEditToLatest(UUID instanceUUID) {
    cleanupEditIntent(instanceUUID, true);
  }

  // If the edit instance fails, the edit intent is just deleted.
  @Transactional
  public static synchronized void deleteEdit(UUID instanceUUID) {
    cleanupEditIntent(instanceUUID, false);
  }

  // Clean the edit intent. Set to current to old on success.
  @Transactional
  private static synchronized void cleanupEditIntent(UUID instanceUUID, boolean success) {
    InstanceInfo instanceInfo = find.byId(instanceUUID);
    InstanceDetails details = getDetails(instanceUUID);

    if (!details.isBeingEdited) {
      throw new IllegalStateException("No edit in progress for " + instanceUUID +
          ", cannot switch out of edit mode.");
    }

    // Save the older index for node numbering, so it can be reused if the new edit is picked up.  
    int oldIndex = InstanceInfo.getUseNodeIndex(instanceUUID);

    if (success) {
      details = InstanceInfo.getEditDetails(instanceUUID);
    }

    details.isBeingEdited = false;
    details.nextNodeIndex = oldIndex;
    instanceInfo.setDetails(details);

    // No more edit, both on success or failure.
    instanceInfo.setEditInstanceDetails(null);

    // Save the instance back.
    instanceInfo.save();
  }

  // Helper method to set/unset a node as a master.
  @Transactional
  public static synchronized void updateEditNodeDetails(UUID instanceUUID,
                                       String nodeName,
                                       boolean isMaster) {
    updateNodeDetails(instanceUUID, nodeName, isMaster, true);
  }
  @Transactional
  public static synchronized void updateNodeDetails(UUID instanceUUID,
                                         String nodeName,
                                         boolean isMaster) {
    updateNodeDetails(instanceUUID, nodeName, isMaster, false);
  }
  @Transactional
  public static synchronized void updateNodeDetails(UUID instanceUUID,
                                       String nodeName,
                                       boolean isMaster,
                                       boolean isEditInstance) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    // Make the desired updates.
    InstanceDetails details = isEditInstance ? InstanceInfo.getEditDetails(instanceUUID)
                                             : InstanceInfo.getDetails(instanceUUID);
    details.nodeDetailsMap.get(nodeName).isMaster = isMaster;
    instanceInfo.setDetails(details);

    // Save the instance back.
    instanceInfo.save();
  }

  /**
   * Check if an entry for the given instance uuid exists.
   * @param instanceUUID
   * @return true if an entry exists for this instance UUID, false otherwise.
   */
  public static boolean exists(UUID instanceUUID) {
    InstanceInfo instanceInfo = find.byId(instanceUUID);
    if (instanceInfo != null) {
      return true;
    }
    return false;
  }

  // Return the subnets covered by this instance.
  public static List<String> getSubnets(UUID instanceUUID) {
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);

    if (details == null) {
      return null;
    }

    return details.subnets;
  }

  /**
   * Read modify write of the state of a being added node. The caller consumes the node, so 
   * marking it as not-being-added here. Caller has to revert on failures.
   *
   * @param instanceUUID
   * @return node details, if such a node exists for this instance UUID, null otherwise.
   */
  @Transactional
  public static synchronized NodeDetails findNewNodeAndUpdateIt(UUID instanceUUID) {
    Collection<NodeDetails> nodes = getEditNodeDetails(instanceUUID);
    NodeDetails newNode = null;
    LOG.info("Found {} nodes.", nodes.size());
    // Find a node being added and set to be not being added, as caller of this api will
    // add it to the instance.
    for (NodeDetails node : nodes) {
      if (node.isBeingAdded) {
        newNode = node;
        break;
      }
    }

    if (newNode != null) {
      LOG.info("Updating {} node {}.", newNode.instance_name, newNode.masterRpcPort);
      newNode.isBeingAdded = false;
      updateEditNodeDetails(instanceUUID, newNode.instance_name, newNode);
    }

    return newNode;
  }

  public static Collection<NodeDetails> getNodeDetails(UUID instanceUUID) {
    InstanceDetails details = InstanceInfo.getDetails(instanceUUID);
    if (details == null) {
      return null;
    }

    return details.nodeDetailsMap.values();
  }

  public static Collection<NodeDetails> getEditNodeDetails(UUID instanceUUID) {
    InstanceDetails edetails = InstanceInfo.getEditDetails(instanceUUID);
    if (edetails == null) {
      return null;
    }

    return edetails.nodeDetailsMap.values();
  }

  public static InstanceDetails getDetails(UUID instanceUUID) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    if (instanceInfo == null) {
      throw new IllegalStateException("Cannot find instance " + instanceUUID);
    }

    // Return the instance details object.
    return Json.fromJson(instanceInfo.instanceDetails, InstanceInfo.InstanceDetails.class);
  }

  public static InstanceDetails getEditDetails(UUID instanceUUID) {
    // Find the instance.
    InstanceInfo instanceInfo = find.byId(instanceUUID);

    if (instanceInfo == null || instanceInfo.editInstanceDetails == null) {
      throw new IllegalStateException("Cannot find instance or details of edit in progress.");
    }

    // Return the instance details object.
    return Json.fromJson(instanceInfo.editInstanceDetails, InstanceInfo.InstanceDetails.class);
  }

  /**
   * Return the list of masters for this instance.
   * @param instanceUUID
   * @param isEdit
   * @return
   */
  public static List<NodeDetails> getNewMasters(UUID instanceUUID) {
    return getMasters(instanceUUID, true);
  }
  public static List<NodeDetails> getMasters(UUID instanceUUID) {
    return getMasters(instanceUUID, false);
  }
  public static List<NodeDetails> getMasters(UUID instanceUUID, boolean isEdit) {
    List<NodeDetails> masters = new LinkedList<NodeDetails>();
    InstanceDetails details = isEdit ? InstanceInfo.getEditDetails(instanceUUID)
                                     : InstanceInfo.getDetails(instanceUUID);
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
   * @param isEdit
   * @return
   */
  public static List<NodeDetails> getNewTServers(UUID instanceUUID) {
    return getTServers(instanceUUID, true);
  }
  public static List<NodeDetails> getTServers(UUID instanceUUID) {
    return getTServers(instanceUUID, false);
  }
  public static List<NodeDetails> getTServers(UUID instanceUUID, boolean isEdit) {
    List<NodeDetails> tservers = new LinkedList<NodeDetails>();
    InstanceDetails details = isEdit ? InstanceInfo.getEditDetails(instanceUUID)
                                     : InstanceInfo.getDetails(instanceUUID);
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

  // Caller ensures transactional behavior.
  private void setEditInstanceDetails(InstanceDetails details) {
    this.editInstanceDetails = Json.toJson(details);
  }

  // Caller ensures transactional behavior.
  private void setDetails(InstanceDetails details) {
    this.instanceDetails = Json.toJson(details);
  }

  /**
   * Sets the being updated state of the current instance and also adds the edit intent to the
   * edit instance column.
   * @param instanceUUID
   * @param subnets
   * @param numNodes
   * @param ybServerPkg
   * @return
   */
  @Transactional
  public static synchronized void setBeingEdited(UUID instanceUUID,
      List<String> subnets,
      int numNodes,
      String ybServerPkg) {
    InstanceInfo instanceInfo = find.byId(instanceUUID);
    if (instanceInfo == null) {
      throw new IllegalStateException("Cannot find instance " + instanceUUID);
    }

    InstanceDetails details = getDetails(instanceUUID);

    if (details.isBeingEdited) {
      throw new IllegalStateException("Edit already in progress for " + instanceUUID +
          " cannot start another.");
    }

    details.isBeingEdited = true;
    instanceInfo.setDetails(details);

    // Create the instance details.
    InstanceDetails editDetails = new InstanceDetails();
    editDetails.subnets = subnets;
    editDetails.numNodes = numNodes;
    editDetails.ybServerPkg = ybServerPkg;
    editDetails.isBeingEdited = false; // This will not be set to true for being-edited one.
    instanceInfo.setEditInstanceDetails(editDetails);

    // Save the instance back.
    instanceInfo.save();
  }

  public static int getUseNodeIndex(UUID instanceUUID) {
    InstanceInfo instanceInfo = find.byId(instanceUUID);
    if (instanceInfo == null) {
      throw new IllegalStateException("Cannot find instance " + instanceUUID);
    }
    InstanceDetails details = getDetails(instanceUUID);
    return details.nextNodeIndex;
  }

  // Helper api to quickly check if this instance is currently being edited. 
  public static boolean isBeingEdited(UUID instanceUUID) {
    InstanceInfo instanceInfo = find.byId(instanceUUID);
    if (instanceInfo == null) {
      throw new IllegalStateException("Cannot find instance " + instanceUUID);
    }

    InstanceDetails details = getDetails(instanceUUID);

    return details.isBeingEdited;
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

    // Set to true when an edit intent on the instance is started.
    public boolean isBeingEdited;

    // Index to use for next node creation.
    public int nextNodeIndex;
  }

  /**
   * Represents all the details of a cloud node that are of interest.
   *
   * NOTE: the names of fields in this class MUST correspond to the output field names of the script
   * 'find_cloud_host.sh' which is in the 'devops' repository.
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
    
    public boolean isBeingAdded = false;
    
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append("Server : " + subnet_id + " "+ public_ip + "/" + private_ip + ":" + masterRpcPort +
                " " + isMaster + " " + isBeingAdded);
      return sb.toString();
    }
  }
}
