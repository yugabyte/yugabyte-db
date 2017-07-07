// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.ConcurrentModificationException;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.Table;
import javax.persistence.UniqueConstraint;

import com.yugabyte.yw.cloud.AWSConstants;
import com.yugabyte.yw.cloud.AWSResourceUtil;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.commissioner.Common;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.Ebean;
import com.avaje.ebean.Model;
import com.avaje.ebean.SqlUpdate;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.data.validation.Constraints;
import play.libs.Json;

@Table(
  uniqueConstraints =
  @UniqueConstraint(columnNames = {"name", "customer_id"})
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
  public Long customerId;

  // The Json serialized version of universeDetails. This is used only in read from and writing to
  // the DB.
  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  private String universeDetailsJson;

  private UniverseDefinitionTaskParams universeDetails;
  public void setUniverseDetails(UniverseDefinitionTaskParams details) {
    universeDetails = details;
  }
  public UniverseDefinitionTaskParams getUniverseDetails() {
    return universeDetails;
  }

  public JsonNode toJson() {
    ObjectNode json = Json.newObject();
    json.put("universeUUID", universeUUID.toString());
    json.put("name", name);
    json.put("creationDate", creationDate.toString());
    json.set("universeDetails", Json.parse(universeDetailsJson));
    json.put("version", version);
    UserIntent userIntent = getUniverseDetails().userIntent;
    try {
      json.set("resources", Json.toJson(getUniverseResourcesUtil(getNodes(), userIntent)));
    } catch (Exception e) {
      json.set("resources", null);
    }
    if (userIntent != null &&
        userIntent.regionList != null &&
        !userIntent.regionList.isEmpty()) {
      List<Region> regions =
        Region.find.where().idIn(userIntent.regionList).findList();

      if (!regions.isEmpty()) {
        json.set("regions", Json.toJson(regions));
        // TODO: change this when we want to deploy across clouds.
        json.set("provider", Json.toJson(regions.get(0).provider));
      }
    }

    if (userIntent != null && !userIntent.gflags.isEmpty()) {
      json.set("gflags", Json.toJson(userIntent.gflags));
    }

    return json;
  }

  public static final Find<UUID, Universe> find = new Find<UUID, Universe>() {
  };

  /**
   * Creates an empty universe.
   * @param name : name of the universe
   * @param universeUUID: UUID of the universe to be created
   * @param customerId: UUID of the customer creating the universe
   * @return the newly created universe
   */
  public static Universe create(String name, UUID universeUUID, Long customerId) {
    // Create the universe object.
    Universe universe = new Universe();
    // Generate a new UUID.
    universe.universeUUID = universeUUID;
    // Set the version of the object to 1.
    universe.version = 1;
    // Set the creation date.
    universe.creationDate = new Date();
    // Set the universe name.
    universe.name = name;
    // Set the customer id.
    universe.customerId = customerId;
    // Create the default universe details. This should be updated after creation.
    universe.universeDetails = new UniverseDefinitionTaskParams();
    universe.universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
    universe.universeDetailsJson = Json.stringify(Json.toJson(universe.universeDetails));
    LOG.debug("Created universe {} with details [{}] with name {}.",
      universe.universeUUID, universe.universeDetailsJson, name);
    // Save the object.
    universe.save();
    return universe;
  }

  /**
   * Returns true if Universe exists with given name
   * @param universeName String which contains the name which is to be checked
   * @return true if universe already exists, false otherwise
   */
  public static boolean checkIfUniverseExists(String universeName) {
    return find.select("universeUUID").where().eq("name", universeName).findRowCount() > 0;
  }

  /**
   * Returns the Universe object given its uuid.
   *
   * @param universeUUID
   * @return the universe object
   */
  public static Universe get(UUID universeUUID) {
    // Find the universe.
    Universe universe = find.byId(universeUUID);
    if (universe == null) {
      throw new RuntimeException("Cannot find universe " + universeUUID);
    }

    universe.universeDetails =
      Json.fromJson(Json.parse(universe.universeDetailsJson), UniverseDefinitionTaskParams.class);

    // Return the universe object.
    return universe;
  }

  public static Set<Universe> get(Set<UUID> universeUUIDs) {
    Set<Universe> universes = new HashSet<Universe>();
    for (UUID universeUUID : universeUUIDs) {
      universes.add(Universe.get(universeUUID));
    }
    return universes;
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
   * @param updater      : lambda which updated the details of this universe when invoked.
   * @return the updated version of the object if successful, or throws an exception.
   */
  public static Universe saveDetails(UUID universeUUID, UniverseUpdater updater) {
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
   *
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
   *
   * @return a collection of nodes in this universe
   */
  public Collection<NodeDetails> getNodes() {
    return getUniverseDetails().nodeDetailsSet;
  }

  /**
   * Returns details about a single node in the universe.
   *
   * @param nodeName
   * @return details about a node, null if it does not exist.
   */
  public NodeDetails getNode(String nodeName) {
    Collection<NodeDetails> nodes = getNodes();
    for (NodeDetails node : nodes) {
      if (node.nodeName.equals(nodeName)) {
        return node;
      }
    }
    return null;
  }

  /**
   * Returns the list of masters for this universe.
   *
   * @return a list of master nodes
   */
  public List<NodeDetails> getMasters() {
    return getServers(ServerType.MASTER);
  }

  /**
   * Return the list of tservers for this universe.
   *
   * @return a list of tserver nodes
   */
  public List<NodeDetails> getTServers() {
    return getServers(ServerType.TSERVER);
  }

  /**
   * Return the list of YQL servers for this universe.
   *
   * @return a list of YQL server nodes
   */
  public List<NodeDetails> getYsqlServers() {
    return getServers(ServerType.YQLSERVER);
  }

  /**
   * Return the list of Redis servers for this universe.
   *
   * @return a list of Redis server nodes
   */
  public List<NodeDetails> getRedisServers() {
    return getServers(ServerType.REDISSERVER);
  }

  private class NodeDetailsPrivateIpComparator implements Comparator<NodeDetails> {
    @Override
    public int compare(NodeDetails n1, NodeDetails n2) {
      return n1.cloudInfo.private_ip.compareTo(n2.cloudInfo.private_ip);
    }
  }

  public List<NodeDetails> getServers(ServerType type) {
    List<NodeDetails> servers = new ArrayList<NodeDetails>();
    UniverseDefinitionTaskParams details = getUniverseDetails();
    for (NodeDetails nodeDetails : details.nodeDetailsSet) {
      switch(type) {
      case YQLSERVER:
        if (nodeDetails.isYqlServer) servers.add(nodeDetails); break;
      case TSERVER:
        if (nodeDetails.isTserver) servers.add(nodeDetails); break;
      case MASTER:
        if (nodeDetails.isMaster) servers.add(nodeDetails); break;
      case REDISSERVER:
        if (nodeDetails.isRedisServer) servers.add(nodeDetails); break;
      default:
        throw new IllegalArgumentException("Unexpected server type " + type);
      }
    }
    // Sort by private IP for deterministic behaviour.
    Collections.sort(servers, new NodeDetailsPrivateIpComparator());
    return servers;
  }

  /**
   * Verifies that the provided list of masters is not empty and that each master is in a queryable
   * state. If so, returns true. Otherwise, returns false.
   *
   * @param masters
   * @return true if all masters are queryable, false otherwise.
   */
  public boolean verifyMastersAreQueryable(List<NodeDetails> masters) {
    if (masters == null || masters.isEmpty()) {
      return false;
    }
    for (NodeDetails details : masters) {
      if (!details.isQueryable()) {
        return false;
      }
    }
    return true;
  }

  public String getMasterAddresses() {
    return getMasterAddresses(true);
  }

  /**
   * Returns a comma separated list of <privateIp:masterRpcPort> for all nodes that have the
   * isMaster flag set to true in this cluster.
   *
   * @param mastersQueryable Set to true if caller wants masters to be queryable, else false.
   * @return a comma separated string of master 'host:port' or, if masters are not queryable, an
   * empty string.
   */
  public String getMasterAddresses(boolean mastersQueryable) {
    List<NodeDetails> masters = getMasters();
    if (mastersQueryable && !verifyMastersAreQueryable(masters)) {
      return "";
    }
    return getHostPortsString(masters, ServerType.MASTER);
  }

  /**
   * Returns a comma separated list of <privateIp:yqlRPCPort> for all nodes that have the
   * isYQLServer flag set to true in this cluster.
   *
   * @return a comma separated string of 'host:port'.
   */
  public String getYQLServerAddresses() {
    return getHostPortsString(getYsqlServers(), ServerType.YQLSERVER);
  }

  /**
   * Returns a comma separated list of <privateIp:redisRPCPort> for all nodes that have the
   * isRedisServer flag set to true in this cluster.
   *
   * @return a comma separated string of 'host:port'.
   */
  public String getRedisServerAddresses() {
    return getHostPortsString(getRedisServers(), ServerType.REDISSERVER);
  }

  // Helper API to create the based on the server type.
  private String getHostPortsString(List<NodeDetails> serverNodes, ServerType type) {
    StringBuilder servers = new StringBuilder();
    for (NodeDetails node : serverNodes) {
      if (node.cloudInfo.private_ip != null) {
        int port = 0;
        switch (type) {
        case YQLSERVER:
          if (node.isYqlServer) port = node.yqlServerRpcPort; break;
        case TSERVER:
          if (node.isTserver) port = node.tserverRpcPort; break;
        case MASTER:
          if (node.isMaster) port = node.masterRpcPort; break;
        case REDISSERVER:
          if (node.isRedisServer) port = node.redisServerRpcPort; break;
        default:
          throw new IllegalArgumentException("Unexpected server type " + type);
        }

        if (servers.length() != 0) {
          servers.append(",");
        }
        servers.append(node.cloudInfo.private_ip).append(":").append(port);
      }
    }
    return servers.toString();
  }

  /**
   * Compares the version of this object with the one in the DB, and updates it if the versions
   * match.
   *
   * @return the new version number after the update if successful, or throws a RuntimeException.
   */
  private int compareAndSwap() {
    // Update the universe details json.
    universeDetailsJson = Json.stringify(Json.toJson(universeDetails));

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
    LOG.debug("Swapped universe {}:{} details to [{}] with new version = {}.",
              universeUUID, this.name, universeDetailsJson, newVersion);
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

  public static UniverseResourceDetails getUniverseResourcesUtil(Collection<NodeDetails> nodes, UniverseDefinitionTaskParams.UserIntent userIntent) throws Exception {
    Common.CloudType cloudType = userIntent.providerType;
    UniverseResourceDetails universeResourceDetails = new UniverseResourceDetails();
    for (NodeDetails node : nodes) {
      if (node.isActive() && cloudType == Common.CloudType.aws) {
        AWSResourceUtil.mergeResourceDetails(userIntent.deviceInfo, userIntent.instanceType,
            node.cloudInfo.az, AvailabilityZone.find.byId(node.azUuid).region.code,
            AWSConstants.Tenancy.Shared, universeResourceDetails);
      }
    }
    return universeResourceDetails;
  }
}
