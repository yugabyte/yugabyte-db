// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.util.Collection;
import java.util.ConcurrentModificationException;
import java.util.Date;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Set;
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
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
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

    if (getUniverseDetails().userIntent != null &&
        getUniverseDetails().userIntent.regionList != null) {
      List<Region> regions =
        Region.find.where().idIn(getUniverseDetails().userIntent.regionList).findList();

      if (!regions.isEmpty()) {
        json.set("regions", Json.toJson(regions));
        // TODO: change this when we want to deploy across clouds.
        json.set("provider", Json.toJson(regions.get(0).provider));
      }
    }

    return json;
  }

  public static final Find<UUID, Universe> find = new Find<UUID, Universe>() {
  };

  /**
   * Creates an empty universe.
   *
   * @param name : name of the universe
   * @return the newly created universe
   */
  public static Universe create(String name, Long customerId) {
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
    List<NodeDetails> masters = new LinkedList<NodeDetails>();
    UniverseDefinitionTaskParams details = getUniverseDetails();
    for (NodeDetails nodeDetails : details.nodeDetailsSet) {
      if (nodeDetails.isMaster) {
        masters.add(nodeDetails);
      }
    }
    return masters;
  }

  /**
   * Return the list of tservers for this universe.
   *
   * @return a list of tserver nodes
   */
  public List<NodeDetails> getTServers() {
    List<NodeDetails> tservers = new LinkedList<NodeDetails>();
    UniverseDefinitionTaskParams details = getUniverseDetails();
    for (NodeDetails nodeDetails : details.nodeDetailsSet) {
      if (nodeDetails.isTserver) {
        tservers.add(nodeDetails);
      }
    }
    return tservers;
  }

  /**
   * Returns a comma separated list of <privateIp:masterRpcPort> for all nodes that have the
   * isMaster flag set to true in this cluster.
   *
   * @return a comma separated string of master 'host:port'
   */
  public String getMasterAddresses() {
    List<NodeDetails> masters = getMasters();
    StringBuilder masterAddresses = new StringBuilder();
    for (NodeDetails nodeDetails : masters) {
      if (nodeDetails.cloudInfo.private_ip != null) {
        if (masterAddresses.length() != 0) {
          masterAddresses.append(",");
        }
        masterAddresses.append(nodeDetails.cloudInfo.private_ip);
        masterAddresses.append(":");
        masterAddresses.append(nodeDetails.masterRpcPort);
      }
    }
    return masterAddresses.toString();
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
}
