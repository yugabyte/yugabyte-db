// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.util.Collection;
import java.util.ConcurrentModificationException;
import java.util.Date;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.Table;
import javax.persistence.UniqueConstraint;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.Ebean;
import com.avaje.ebean.Model;
import com.avaje.ebean.SqlUpdate;
import com.fasterxml.jackson.annotation.JsonFormat;

import play.data.validation.Constraints;
import play.libs.Json;

@Table(
    uniqueConstraints=
        @UniqueConstraint(columnNames={"name", "customer_id"})
)
@Entity
public class Universe extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Universe.class);

  // The universe UUID.
  @Id
  public UUID universeUUID;

  // The version number of the object. This is used to synchronize updates from multiple clients.
  @Constraints.Required
  @Column(nullable = false)
  public int version;

  // Tracks when the universe was created.
  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd hh:mm:ss")
  public Date creationDate;

  // The universe name.
  public String name;

  // The customer id, needed only to enforce unique universe names for a customer.
  @Constraints.Required
  public int customerId;

  // The Json serialized version of universeDetails. This is used only in read from and writing to
  // the DB.
  @Constraints.Required
  @Column(columnDefinition = "LONGTEXT", nullable = false)
  private String universeDetailsJson;

  // Once deserialized, the object version of the serialized value is stored in this variable.
  public UniverseDetails universeDetails;

  public static final Find<UUID, Universe> find = new Find<UUID, Universe>(){};

  /**
   * Creates an empty universe.
   *
   * @param name : name of the universe
   * @return the newly created universe
   */
  public static Universe create(String name, int customerId) {
    // Create the universe object.
    Universe universe = new Universe();

    // Generate a new UUID.
    universe.universeUUID = UUID.randomUUID();
    // Set the version of the object to 1.
    universe.version = 1;
    // Set the creation date.
    universe.creationDate = new Date();
    // Set the universe name.
    universe.name = name;
    // Set the customer id.
    universe.customerId = customerId;
    // Create the default universe details. This should be updated after creation.
    universe.universeDetails = new UniverseDetails();
    universe.universeDetailsJson = Json.stringify(Json.toJson(universe.universeDetails));
    LOG.debug("Created universe " + universe.universeUUID + " with name " + universe.name +
              "details [" + universe.universeDetailsJson + "]");
    // Save the object.
    universe.save();
    return universe;
  }

  /**
   * Returns the UserUniverse object given its uuid.
   * @param universeUUID
   * @return the universe object
   */
  public static Universe get(UUID universeUUID) {
    // Find the universe.
    Universe universe = find.byId(universeUUID);
    if (universe == null) {
      throw new IllegalStateException("Cannot find universe " + universeUUID);
    }
    // Set the universe details object.
    universe.universeDetails = Json.fromJson(Json.parse(universe.universeDetailsJson),
                                             Universe.UniverseDetails.class);

    // Return the universe object.
    return universe;
  }


  /**
   * Interface using which we specify a callback to update the universe object. This is passed into
   * the save method.
   */
  public static interface UniverseUpdater {
    void run(Universe universe);
  }

  /**
   * Updates the details of the universe if the possible using the update lambda function.
   *
   * @param universeUUID : the universe UUID that we want to update
   * @param updater : lambda which updated the details of this universe when invoked.
   * @return the updated version of the object if successful, or throws an exception.
   */
  public static Universe save(UUID universeUUID, UniverseUpdater updater) {
    int numRetriesLeft = 10;
    long sleepTimeMillis = 100;
    // Try the read and update for a few times till it succeeds.
    Universe universe = null;
    while (numRetriesLeft > 0) {
      // Get the universe info.
      universe = Universe.get(universeUUID);
      // Update the universe object which is supplied as a lambda function.
      updater.run(universe);
      // Save the universe object by doing a compare and swap.
      try {
        universe.compareAndSwap();
        break;
      } catch (ConcurrentModificationException e) {
        // Decrement retries.
        numRetriesLeft--;
        // If we are out of retries, fail the task.
        if (numRetriesLeft == 0) {
          throw e;
        }
        // If we have more retries left, wait and retry.
        try {
          Thread.sleep(sleepTimeMillis);
        } catch (InterruptedException e1) {
          LOG.error("Error while sleeping", e1);
        }
        continue;
      }
    }
    return universe;
  }

  /**
   * Deletes the universe entry with the given UUID.
   * @param universeUUID : uuid of the universe.
   */
  public static void delete(UUID universeUUID) {
    // First get the universe.
    Universe universe = Universe.get(universeUUID);
    // Make sure this universe has been locked.
    assert !universe.universeDetails.updateInProgress;
    // Delete the universe.
    LOG.info("Deleting universe " + universe.name + ":" + universeUUID);
    universe.delete();
  }

  /**
   * Returns the list of nodes in the universe.
   * @return a collection of nodes in this universe
   */
  public Collection<NodeDetails> getNodes() {
    UniverseDetails details = Universe.get(universeUUID).universeDetails;
    if (details == null) {
      return null;
    }

    return details.nodeDetailsMap.values();
  }

  /**
   * Returns details about a single node in the universe.
   * @param nodeName
   * @return details about a node
   */
  public NodeDetails getNode(String nodeName) {
    UniverseDetails details = Universe.get(universeUUID).universeDetails;
    if (details == null) {
      return null;
    }
    return details.nodeDetailsMap.get(nodeName);
  }

  /**
   * Returns the list of masters for this universe.
   * @return a list of master nodes
   */
  public List<NodeDetails> getMasters() {
    List<NodeDetails> masters = new LinkedList<NodeDetails>();
    UniverseDetails details = Universe.get(universeUUID).universeDetails;
    for (NodeDetails nodeDetails : details.nodeDetailsMap.values()) {
      if (nodeDetails.isMaster) {
        masters.add(nodeDetails);
      }
    }
    return masters;
  }

  /**
   * Return the list of tservers for this universe.
   * @return a list of tserver nodes
   */
  public List<NodeDetails> getTServers() {
    List<NodeDetails> tservers = new LinkedList<NodeDetails>();
    UniverseDetails details = Universe.get(universeUUID).universeDetails;
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
   * @return a comma separated string of master 'host:port'
   */
  public String getMasterAddresses() {
    List<NodeDetails> masters = getMasters();
    StringBuilder masterAddresses = new StringBuilder();
    for (NodeDetails nodeDetails : masters) {
      if (masterAddresses.length() != 0) {
        masterAddresses.append(",");
      }
      masterAddresses.append(nodeDetails.private_ip);
      masterAddresses.append(":");
      masterAddresses.append(nodeDetails.masterRpcPort);
    }
    return masterAddresses.toString();
  }

  /**
   * Compares the version of this object with the one in the DB, and updates it if the versions
   * match.
   * @return the new version number after the update if successful, or throws a RuntimeException.
   */
  private int compareAndSwap() {
    // Serialize the universe details.
    this.universeDetailsJson = Json.stringify(Json.toJson(this.universeDetails));

    // Create the new version number.
    int newVersion = this.version + 1;

    // Save the object if the version is the same.
    String updateQuery = "UPDATE universe " +
                         "SET universe_details_json = :universeDetails, version = :newVersion " +
                         "WHERE universe_uuid = :universeUUID AND version = :curVersion";
    SqlUpdate update = Ebean.createSqlUpdate(updateQuery);
    update.setParameter("universeDetails", universeDetailsJson);
    update.setParameter("universeUUID", universeUUID);
    update.setParameter("curVersion", this.version);
    update.setParameter("newVersion", newVersion);
    LOG.debug("Swapped universe " + universeUUID + ":" + this.name +
              " details to [" + universeDetailsJson + "] with new version = " + this.version);
    int modifiedCount = Ebean.execute(update);

    // Check if the save was not successful.
    if (modifiedCount == 0) {
      // Throw an exception as the save was not successful.
      throw new ConcurrentModificationException("Stale version " + this.version);
    } else if (modifiedCount > 1) {
      // Exactly one row should have been modified. Otherwise fatal.
      LOG.error("Running query [" + updateQuery + "] updated " + modifiedCount + " rows");
      System.exit(1);
    }

    // Update and return the current version number.
    this.version = newVersion;
    return this.version;
  }

  /**
   * Details of a universe.
   */
  public static class UniverseDetails {
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

  /**
   * The user defined intent for the universe.
   */
  public static class UserIntent {
    // The replication factor.
    public int replicationFactor;

    // Determines if this universe is a single or multi AZ deployment.
    public Boolean isMultiAZ;

    // The list of regions that the user wants to place data replicas into.
    public List<UUID> regionList;

    // The regions that the user wants to nominate as the preferred region. This makes sense only for
    // a multi-region setup.
    public UUID preferredRegion;
  }

  /**
   * The placement info is a tree. The first level contains a list of clouds. Every cloud contains a
   * list of regions. Each region has a list of AZs. The number of leaves in this tree should be
   * equal to the replication factor, and each leaf defines the data placement (by virtue of its
   * path from the first level).
   */
  public static class PlacementInfo {
    // The list of clouds to place data in.
    public List<PlacementCloud> cloudList;
  }

  public static class PlacementCloud {
    // The cloud provider id.
    public UUID uuid;
    // The cloud provider name.
    public String name;
    // The list of region in this cloud we want to place data in.
    public List<PlacementRegion> regionList;
  }

  public static class PlacementRegion {
    // The region provider id.
    public UUID uuid;
    // The region name.
    public String name;
    // The list of AZs inside this region into which we want to place data.
    public List<PlacementAZ> azList;
  }

  public static class PlacementAZ {
    // The AZ provider id.
    public UUID uuid;
    // The AZ name.
    public String name;
    // The number of copies of data we should place into this AZ.
    public int replicationFactor;
    // The subnet in the AZ.
    public String subnet;
  }

  /**
   * Represents all the details of a cloud node that are of interest.
   *
   * NOTE: the names of fields in this class MUST correspond to the output field names of the script
   * 'find_cloud_host.sh' which is in the 'devops' repository.
   */
  public static class NodeDetails {
    // The id of the node. This is usually present in the node name.
    public int nodeIdx = -1;
    // Name of the node.
    public String instance_name;
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

    // True if this node is a master, along with port info.
    public boolean isMaster;
    public int masterHttpPort = 7000;
    public int masterRpcPort = 7100;

    // True if this node is a tserver, along with port info.
    public boolean isTserver;
    public int tserverHttpPort = 9000;
    public int tserverRpcPort = 9100;

    @Override
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append("name: ")
        .append(instance_name).append(".").append(az).append(".")
        .append(region).append(".").append(cloud)
        .append(", ip: ").append(private_ip)
        .append(", isMaster: ").append(isMaster)
        .append(", isTserver: ").append(isTserver);
      return sb.toString();
    }
  }
}
