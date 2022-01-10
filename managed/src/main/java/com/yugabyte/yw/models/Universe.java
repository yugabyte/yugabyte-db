// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableSet;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.PortType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.password.RedactingService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import io.ebean.Ebean;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.SqlUpdate;
import io.ebean.annotation.DbJson;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.ConcurrentModificationException;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.Table;
import javax.persistence.Transient;
import javax.persistence.UniqueConstraint;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;
import play.api.Play;
import play.data.validation.Constraints;
import play.libs.Json;

@Table(uniqueConstraints = @UniqueConstraint(columnNames = {"name", "customer_id"}))
@Entity
public class Universe extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Universe.class);
  public static final String DISABLE_ALERTS_UNTIL = "disableAlertsUntilSecs";
  public static final String TAKE_BACKUPS = "takeBackups";
  public static final String HELM2_LEGACY = "helm2Legacy";

  private static void checkUniverseInCustomer(UUID universeUUID, Customer customer) {
    if (!customer.getUniverseUUIDs().contains(universeUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Universe UUID: %s doesn't belong " + "to Customer UUID: %s",
              universeUUID, customer.uuid));
    }
  }

  public static Universe getValidUniverseOrBadRequest(UUID universeUUID, Customer customer) {
    Universe universe = getOrBadRequest(universeUUID);
    checkUniverseInCustomer(universeUUID, customer);
    return universe;
  }

  public enum HelmLegacy {
    V3,
    V2TO3
  }

  // The universe UUID.
  @Id public UUID universeUUID;

  // The version number of the object. This is used to synchronize updates from multiple clients.
  @Constraints.Required
  @Column(nullable = false)
  public int version;

  // Tracks when the universe was created.
  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssZ")
  public Date creationDate;

  // The universe name.
  public String name;

  // The customer id, needed only to enforce unique universe names for a customer.
  @Constraints.Required public Long customerId;

  @DbJson
  @Column(columnDefinition = "TEXT")
  private Map<String, String> config;

  @JsonIgnore
  public void setConfig(Map<String, String> newConfig) {
    LOG.info("Setting config {} on universe {} [ {} ]", Json.toJson(config), name, universeUUID);
    this.config = newConfig;
    this.save();
  }

  public void updateConfig(Map<String, String> newConfig) {
    Map<String, String> tmp = getConfig();
    tmp.putAll(newConfig);
    setConfig(tmp);
  }

  @JsonIgnore
  public Map<String, String> getConfig() {
    return config == null ? new HashMap<>() : config;
  }

  // The Json serialized version of universeDetails. This is used only in read from and writing to
  // the DB.
  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  private String universeDetailsJson;

  @Transient private UniverseDefinitionTaskParams universeDetails;

  public void setUniverseDetails(UniverseDefinitionTaskParams details) {
    universeDetails = details;
  }

  public UniverseDefinitionTaskParams getUniverseDetails() {
    return universeDetails;
  }

  public UUID getUniverseUUID() {
    return universeUUID;
  }

  public void resetVersion() {
    this.version = -1;
    this.update();
  }

  @JsonIgnore
  public List<String> getVersions() {
    if (null == universeDetails || null == universeDetails.clusters) {
      return new ArrayList<>();
    }
    return universeDetails
        .clusters
        .stream()
        .filter(c -> c != null && c.userIntent != null)
        .map(c -> c.userIntent.ybSoftwareVersion)
        .collect(Collectors.toList());
  }

  public static final Finder<UUID, Universe> find = new Finder<UUID, Universe>(Universe.class) {};

  // Prefix added to read only node.
  public static final String READONLY = "-readonly";

  // Prefix added to node Index of each read replica node.
  public static final String NODEIDX_PREFIX = "-n";

  /**
   * Creates an empty universe.
   *
   * @param taskParams: The details that will describe the universe.
   * @param customerId: UUID of the customer creating the universe
   * @return the newly created universe
   */
  public static Universe create(UniverseDefinitionTaskParams taskParams, Long customerId) {
    // Create the universe object.
    Universe universe = new Universe();
    // Generate a new UUID.
    universe.universeUUID = taskParams.universeUUID;
    // Set the version of the object to 1.
    universe.version = 1;
    // Set the creation date.
    universe.creationDate = new Date();
    // Set the universe name.
    universe.name = taskParams.getPrimaryCluster().userIntent.universeName;
    // Set the customer id.
    universe.customerId = customerId;
    // Create the default universe details. This should be updated after creation.
    universe.universeDetails = taskParams;
    universe.universeDetailsJson =
        Json.stringify(RedactingService.filterSecretFields(Json.toJson(universe.universeDetails)));
    LOG.info("Created db entry for universe {} [{}]", universe.name, universe.universeUUID);
    LOG.debug(
        "Details for universe {} [{}] : [{}].",
        universe.name,
        universe.universeUUID,
        universe.universeDetailsJson);
    // Save the object.
    universe.save();
    return universe;
  }

  /**
   * Returns true if Universe exists with given name
   *
   * @param universeName String which contains the name which is to be checked
   * @return true if universe already exists, false otherwise
   */
  @Deprecated
  public static boolean checkIfUniverseExists(String universeName) {
    return find.query().select("universeUUID").where().eq("name", universeName).findCount() > 0;
  }

  /**
   * Fetch ONLY the universeUUID field for all universes. WARNING: Returns partially filled Universe
   * objects!!
   *
   * @return list of UUIDs of all universes
   */
  public static Set<UUID> getAllUUIDs(Customer customer) {
    return ImmutableSet.copyOf(
        find.query().where().eq("customer_id", customer.getCustomerId()).findIds());
  }

  public static Set<Universe> getAllWithoutResources(Customer customer) {
    List<Universe> rawList =
        find.query().where().eq("customer_id", customer.getCustomerId()).findList();
    return rawList.stream().peek(Universe::fillUniverseDetails).collect(Collectors.toSet());
  }

  public static Set<Universe> getAllWithoutResources(Set<UUID> uuids) {
    ExpressionList<Universe> query = find.query().where();
    CommonUtils.appendInClause(query, "universeUUID", uuids);
    List<Universe> rawList = query.findList();
    return rawList.stream().peek(Universe::fillUniverseDetails).collect(Collectors.toSet());
  }

  /**
   * Returns the Universe object given its uuid.
   *
   * @return the universe object
   */
  public static Universe getOrBadRequest(UUID universeUUID) {
    return maybeGet(universeUUID)
        .orElseThrow(
            () ->
                new PlatformServiceException(BAD_REQUEST, "Cannot find universe " + universeUUID));
  }

  public static Optional<Universe> maybeGet(UUID universeUUID) {
    // Find the universe.
    Universe universe = find.byId(universeUUID);
    if (universe == null) {
      LOG.trace("Cannot find universe {}", universeUUID);
      return Optional.empty();
    }

    fillUniverseDetails(universe);

    // Return the universe object.
    return Optional.of(universe);
  }

  public static Set<Universe> getAllPresent(Set<UUID> universeUUIDs) {
    return universeUUIDs
        .stream()
        .map(Universe::maybeGet)
        .filter(Optional::isPresent)
        .map(Optional::get)
        .collect(Collectors.toSet());
  }

  public static Universe getUniverseByName(String universeName) {
    return find.query().where().eq("name", universeName).findOne();
  }

  public static Optional<Universe> maybeGetUniverseByName(Long customerId, String universeName) {
    return find.query()
        .where()
        .eq("customerId", customerId)
        .eq("name", universeName)
        .findOneOrEmpty()
        .map(Universe::fillUniverseDetails);
  }

  /**
   * Interface using which we specify a callback to update the universe object. This is passed into
   * the save method.
   */
  public interface UniverseUpdater {
    void run(Universe universe);
  }

  // TODO: Investigate why not use transactions for this
  // TODO: synchronized in static scope - investigate what state it is protecting
  // Helper api to make an atomic read of universe version, and compare and swap the
  // updated version to disk.
  private static synchronized Universe readModifyWrite(
      UUID universeUUID, UniverseUpdater updater, boolean incrementVersion) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    // Update the universe object which is supplied as a lambda function.
    boolean updateSucceeded = false;
    try {
      updater.run(universe);
      updateSucceeded = true;
    } catch (Exception e) {
      LOG.debug("Error running universe updater", e);
      throw e;
    } finally {
      // Save the universe object by doing a compare and swap.
      universe.compareAndSwap(updateSucceeded /* updateDetails */, incrementVersion);
    }

    return universe;
  }

  /**
   * Updates the details of the universe if possible using the update lambda function.
   *
   * @param universeUUID : the universe UUID that we want to update
   * @param updater : lambda which updated the details of this universe when invoked.
   * @return the updated version of the object if successful, or throws an exception.
   */
  public static Universe saveDetails(UUID universeUUID, UniverseUpdater updater) {
    return Universe.saveDetails(universeUUID, updater, true);
  }

  public static Universe saveDetails(
      UUID universeUUID, UniverseUpdater updater, boolean incrementVersion) {
    int numRetriesLeft = 10;
    long sleepTimeMillis = 100;
    // Try the read and update for a few times till it succeeds.
    Universe universe = null;
    while (numRetriesLeft > 0) {
      try {
        universe = readModifyWrite(universeUUID, updater, incrementVersion);
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
    Universe universe = Universe.getOrBadRequest(universeUUID);
    // Make sure this universe has been locked.
    // TODO: fixme. Useless check. java asserts are turned off by default in production code!!!
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
   * Checks if there is any node in a transit state across the universe.
   *
   * @return true if there is any such node.
   */
  public boolean nodesInTransit() {
    return getUniverseDetails().nodeDetailsSet.stream().anyMatch(NodeDetails::isInTransit);
  }

  public NodeDetails getNodeOrBadRequest(String nodeName) {
    return maybeGetNode(nodeName)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Invalid Node " + nodeName + " for Universe"));
  }

  /**
   * Returns details about a single node in the universe.
   *
   * @return details about a node, null if it does not exist.
   */
  public NodeDetails getNode(String nodeName) {
    return maybeGetNode(nodeName).orElse(null);
  }

  public Optional<NodeDetails> maybeGetNode(String nodeName) {
    Collection<NodeDetails> nodes = getNodes();
    for (NodeDetails node : nodes) {
      if (node.nodeName != null && node.nodeName.equals(nodeName)) {
        return Optional.of(node);
      }
    }
    return Optional.empty();
  }

  /**
   * Returns details about a single node by ip address in the universe.
   *
   * @param nodeIP Private IP address of the node
   * @return details about a node, null if it does not exist.
   */
  public NodeDetails getNodeByPrivateIP(String nodeIP) {
    Collection<NodeDetails> nodes = getNodes();
    for (NodeDetails node : nodes) {
      if (node.cloudInfo.private_ip.equals(nodeIP)) {
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
  public List<NodeDetails> getYqlServers() {
    return getServers(ServerType.YQLSERVER);
  }

  /**
   * Return the list of YSQL servers for this universe.
   *
   * @return a list of YSQL server nodes
   */
  public List<NodeDetails> getYsqlServers() {
    return getServers(ServerType.YSQLSERVER);
  }

  /**
   * Return the list of Redis servers for this universe.
   *
   * @return a list of Redis server nodes
   */
  public List<NodeDetails> getRedisServers() {
    return getServers(ServerType.REDISSERVER);
  }

  private static class NodeDetailsPrivateIpComparator implements Comparator<NodeDetails> {
    @Override
    public int compare(NodeDetails n1, NodeDetails n2) {
      return n1.cloudInfo.private_ip.compareTo(n2.cloudInfo.private_ip);
    }
  }

  public List<NodeDetails> getServers(ServerType type) {
    List<NodeDetails> servers = new ArrayList<>();
    UniverseDefinitionTaskParams details = getUniverseDetails();
    Set<NodeDetails> filteredNodeDetails =
        details
            .nodeDetailsSet
            .stream()
            .filter(n -> n.cloudInfo.private_ip != null)
            .collect(Collectors.toSet());
    for (NodeDetails nodeDetails : filteredNodeDetails) {
      switch (type) {
        case YQLSERVER:
          if (nodeDetails.isYqlServer && nodeDetails.isTserver) servers.add(nodeDetails);
          break;
        case YSQLSERVER:
          if (nodeDetails.isYsqlServer && nodeDetails.isTserver) servers.add(nodeDetails);
          break;
        case TSERVER:
          if (nodeDetails.isTserver) servers.add(nodeDetails);
          break;
        case MASTER:
          if (nodeDetails.isMaster) servers.add(nodeDetails);
          break;
        case REDISSERVER:
          if (nodeDetails.isRedisServer && nodeDetails.isTserver) servers.add(nodeDetails);
          break;
        default:
          throw new IllegalArgumentException("Unexpected server type " + type);
      }
    }
    // Sort by private IP for deterministic behaviour.
    servers.sort(new NodeDetailsPrivateIpComparator());
    return servers;
  }

  /**
   * Verifies that the provided list of masters is not empty and that each master is in a queryable
   * state. If so, returns true. Otherwise, returns false.
   *
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

  public String getKubernetesMasterAddresses() {
    return getMasters().stream().map((m) -> m.nodeName).collect(Collectors.joining(","));
  }

  public String getMasterAddresses() {
    return getMasterAddresses(false);
  }

  /**
   * Returns a comma separated list of <privateIp:masterRpcPort> for all nodes that have the
   * isMaster flag set to true in this cluster.
   *
   * @param mastersQueryable Set to true if caller wants masters to be queryable, else false.
   * @return a comma separated string of master 'host:port' or, if masters are not queryable, an
   *     empty string.
   */
  public String getMasterAddresses(boolean mastersQueryable) {
    List<NodeDetails> masters = getMasters();
    if (mastersQueryable && !verifyMastersAreQueryable(masters)) {
      return "";
    }
    return getHostPortsString(masters, ServerType.MASTER, PortType.RPC);
  }

  /**
   * Returns a comma separated list of <privateIp:tserverHTTPPort> for all tservers of this
   * universe.
   *
   * @return a comma separated string of 'host:port'.
   */
  public String getTserverHTTPAddresses() {
    return getHostPortsString(getTServers(), ServerType.TSERVER, PortType.HTTP);
  }

  /**
   * Returns the certificate path in case node to node TLS is enabled.
   *
   * @return path to the certfile.
   */
  public String getCertificateNodetoNode() {
    UniverseDefinitionTaskParams details = this.getUniverseDetails();
    if (details.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt) {
      // This means there must be a root CA associated with it.
      return CertificateInfo.get(details.rootCA).certificate;
    }
    return null;
  }

  /**
   * Returns the certificate path in case client to node TLS is enabled.
   *
   * @return path to the certfile.
   */
  public String getCertificateClientToNode() {
    UniverseDefinitionTaskParams details = this.getUniverseDetails();
    if (details.getPrimaryCluster().userIntent.enableClientToNodeEncrypt) {
      // This means there must be a root CA associated with it.
      if (details.rootAndClientRootCASame) {
        return CertificateInfo.get(details.rootCA).certificate;
      }
      return CertificateInfo.get(details.clientRootCA).certificate;
    }
    return null;
  }

  /**
   * Returns a comma separated list of <privateIp:yqlRPCPort> for all nodes that have the
   * isYQLServer flag set to true in this cluster.
   *
   * @return a comma separated string of 'host:port'.
   */
  public String getYQLServerAddresses() {
    return getHostPortsString(getYqlServers(), ServerType.YQLSERVER, PortType.RPC);
  }

  /**
   * Returns a comma separated list of <privateIp:ysqlRPCPort> for all nodes that have the
   * isYSQLServer flag set to true in this cluster.
   *
   * @return a comma separated string of 'host:port'.
   */
  public String getYSQLServerAddresses() {
    return getHostPortsString(getYsqlServers(), ServerType.YSQLSERVER, PortType.RPC);
  }

  /**
   * Returns a comma separated list of <privateIp:redisRPCPort> for all nodes that have the
   * isRedisServer flag set to true in this cluster.
   *
   * @return a comma separated string of 'host:port'.
   */
  public String getRedisServerAddresses() {
    return getHostPortsString(getRedisServers(), ServerType.REDISSERVER, PortType.RPC);
  }

  // Helper API to return port number based on port type.
  private static int selectPort(PortType portType, int rpcPort, int httpPort) {
    if (portType != PortType.HTTP && portType != PortType.RPC) {
      throw new IllegalArgumentException("Unexpected port type " + portType);
    }
    int port = 0;
    switch (portType) {
      case RPC:
        port = rpcPort;
        break;
      case HTTP:
        port = httpPort;
        break;
    }
    return port;
  }

  // Helper API to create the based on the server type.
  private String getHostPortsString(
      List<NodeDetails> serverNodes, ServerType type, PortType portType) {
    StringBuilder servers = new StringBuilder();
    for (NodeDetails node : serverNodes) {
      if (node.cloudInfo.private_ip != null) {
        int port = 0;
        switch (type) {
          case YQLSERVER:
            if (node.isYqlServer) {
              port = selectPort(portType, node.yqlServerRpcPort, node.yqlServerHttpPort);
            }
            break;
          case YSQLSERVER:
            if (node.isYsqlServer) {
              port = selectPort(portType, node.ysqlServerRpcPort, node.ysqlServerHttpPort);
            }
            break;
          case TSERVER:
            if (node.isTserver) {
              port = selectPort(portType, node.tserverRpcPort, node.tserverHttpPort);
            }
            break;
          case MASTER:
            if (node.isMaster) {
              port = selectPort(portType, node.masterRpcPort, node.masterHttpPort);
            }
            break;
          case REDISSERVER:
            if (node.isRedisServer) {
              port = selectPort(portType, node.redisServerRpcPort, node.redisServerHttpPort);
            }
            break;
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
   * @param updateDetails whether to update universe details or not
   * @param incrementVersion whether to increment the version or not
   * @return the current version of the universe metadata
   */
  private int compareAndSwap(boolean updateDetails, boolean incrementVersion) {
    // Update the universe details json.
    universeDetailsJson = Json.stringify(Json.toJson(universeDetails));

    // Create the new version number.
    int newVersion = incrementVersion ? this.version + 1 : this.version;

    // Save the object if the version is the same.
    String updateQuery =
        updateDetails
            ? "UPDATE universe "
                + "SET universe_details_json = :universeDetails, version = :newVersion "
                + "WHERE universe_uuid = :universeUUID AND version = :curVersion"
            : "UPDATE universe "
                + "SET version = :newVersion "
                + "WHERE universe_uuid = :universeUUID AND version = :curVersion";

    SqlUpdate update = Ebean.createSqlUpdate(updateQuery);
    if (updateDetails) {
      update.setParameter("universeDetails", universeDetailsJson);
    }

    update.setParameter("universeUUID", universeUUID);
    update.setParameter("curVersion", this.version);
    update.setParameter("newVersion", newVersion);
    LOG.trace(
        "Swapped universe {}:{} details to [{}] with new version = {}.",
        universeUUID,
        this.name,
        universeDetailsJson,
        newVersion);
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
   * Returns the list of nodes in a given cluster in the universe.
   *
   * @param clusterUUID UUID of the cluster to get the list of nodes.
   * @return a collection of nodes in a given cluster in this universe.
   */
  public Collection<NodeDetails> getNodesInCluster(UUID clusterUUID) {
    return getUniverseDetails().getNodesInCluster(clusterUUID);
  }

  /**
   * Returns the cluster with the given uuid in the universe.
   *
   * @param clusterUUID UUID of the cluster to check.
   * @return The cluster object if it is in this universe, null otherwise.
   */
  public Cluster getCluster(UUID clusterUUID) {
    return getUniverseDetails().getClusterByUuid(clusterUUID);
  }

  /**
   * Returns the cluster to which this node belongs.
   *
   * @param nodeName name of the node.
   * @param universe universe which can contain the node.
   * @return cluster info from the universe which contains this node.
   */
  public static Cluster getCluster(Universe universe, String nodeName) {
    if (!nodeName.contains(READONLY)) { // BAD
      return universe.getUniverseDetails().getPrimaryCluster();
    }

    for (Cluster cluster : universe.getUniverseDetails().getReadOnlyClusters()) {
      if (nodeName.contains(Universe.READONLY + cluster.index + Universe.NODEIDX_PREFIX)) {
        return cluster;
      }
    }

    return null;
  }

  /**
   * Find the current master leader in the universe
   *
   * @return the host (private_ip) and port of the current master leader in the universe or null if
   *     not found
   */
  public HostAndPort getMasterLeader() {
    final String masterAddresses = getMasterAddresses();
    final String cert = getCertificateNodetoNode();
    final YBClientService ybService = Play.current().injector().instanceOf(YBClientService.class);
    final YBClient client = ybService.getClient(masterAddresses, cert);
    try {
      return client.getLeaderMasterHostAndPort();
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
  }

  /**
   * Find the current master leader in the universe
   *
   * @return a String of the private_ip of the current master leader in the universe or an empty
   *     string if not found
   */
  public String getMasterLeaderHostText() {
    final HostAndPort masterLeader = getMasterLeader();
    if (masterLeader == null) return "";
    return masterLeader.getHost();
  }

  public boolean universeIsLocked() {
    return getUniverseDetails().updateInProgress;
  }

  public boolean nodeExists(String host, int port) {
    return getUniverseDetails()
        .nodeDetailsSet
        .parallelStream()
        .anyMatch(
            n ->
                n.cloudInfo.private_ip.equals(host)
                    && (port == n.masterHttpPort
                        || port == n.tserverHttpPort
                        || port == n.ysqlServerHttpPort
                        || port == n.yqlServerHttpPort
                        || port == n.redisServerHttpPort
                        || port == n.nodeExporterPort));
  }

  public void incrementVersion() {
    Universe.saveDetails(universeUUID, ignoreUniverse -> {});
  }

  public static Set<Universe> universeDetailsIfCertsExists(UUID certUUID, UUID customerUUID) {
    return Customer.get(customerUUID)
        .getUniverses()
        .stream()
        .filter(
            s ->
                (s.getUniverseDetails().rootCA != null
                        && s.getUniverseDetails().rootCA.equals(certUUID))
                    || (s.getUniverseDetails().clientRootCA != null
                        && s.getUniverseDetails().clientRootCA.equals(certUUID)))
        .collect(Collectors.toSet());
  }

  public static Set<Universe> universeDetailsIfReleaseExists(String version) {
    Set<Universe> universes = new HashSet<Universe>();
    Customer.getAll()
        .forEach(customer -> universes.addAll(Customer.get(customer.getUuid()).getUniverses()));
    Set<Universe> universesWithGivenRelease = new HashSet<Universe>();
    for (Universe u : universes) {
      List<Cluster> clusters = u.getUniverseDetails().clusters;
      for (Cluster c : clusters) {
        if (c.userIntent.ybSoftwareVersion != null
            && c.userIntent.ybSoftwareVersion.equals(version)) {
          universesWithGivenRelease.add(u);
          break;
        }
      }
    }
    return universesWithGivenRelease;
  }

  public static boolean existsCertificate(UUID certUUID, UUID customerUUID) {
    return universeDetailsIfCertsExists(certUUID, customerUUID).size() != 0;
  }

  public static boolean existsRelease(String version) {
    return universeDetailsIfReleaseExists(version).size() != 0;
  }

  static boolean isUniversePaused(UUID uuid) {
    Universe universe = maybeGet(uuid).orElse(null);
    if (universe == null) {
      return false;
    }
    return universe.getUniverseDetails().universePaused;
  }

  private static Universe fillUniverseDetails(Universe universe) {
    JsonNode detailsJson = Json.parse(universe.universeDetailsJson);
    universe.universeDetails = Json.fromJson(detailsJson, UniverseDefinitionTaskParams.class);

    // For backwards compatibility from {universeDetails: {"userIntent": <foo>, "placementInfo":
    // <bar>}}
    // to {universeDetails: {clusters: [{"userIntent": <foo>, "placementInfo": <bar>},...]}}
    if (detailsJson != null
        && !detailsJson.isNull()
        && (!detailsJson.has("clusters") || detailsJson.get("clusters").size() == 0)) {
      UserIntent userIntent = Json.fromJson(detailsJson.get("userIntent"), UserIntent.class);
      PlacementInfo placementInfo =
          Json.fromJson(detailsJson.get("placementInfo"), PlacementInfo.class);
      universe.universeDetails.upsertPrimaryCluster(userIntent, placementInfo);
    }
    return universe;
  }
}
