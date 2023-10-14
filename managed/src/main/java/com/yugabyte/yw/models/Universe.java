// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonManagedReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableSet;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.PortType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TransactionUtil;
import io.ebean.DB;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.SqlQuery;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Transactional;
import io.ebean.annotation.TxIsolation;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.OneToMany;
import javax.persistence.PostRemove;
import javax.persistence.Table;
import javax.persistence.Transient;
import javax.persistence.UniqueConstraint;
import lombok.Builder;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import org.yb.client.YBClient;
import play.data.validation.Constraints;
import play.libs.Json;

@Table(uniqueConstraints = @UniqueConstraint(columnNames = {"name", "customer_id"}))
@Entity
@Getter
@Setter
public class Universe extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Universe.class);
  public static final String DISABLE_ALERTS_UNTIL = "disableAlertsUntilSecs";
  public static final String TAKE_BACKUPS = "takeBackups";
  public static final String HELM2_LEGACY = "helm2Legacy";
  public static final String DUAL_NET_LEGACY = "dualNetLegacy";
  public static final String USE_CUSTOM_IMAGE = "useCustomImage";
  public static final String IS_MULTIREGION = "isMultiRegion";
  // Flag for whether we have https on for master/tserver UI
  public static final String HTTPS_ENABLED_UI = "httpsEnabledUI";

  // This is a key lock for Universe by UUID.
  public static final KeyLock<UUID> UNIVERSE_KEY_LOCK = new KeyLock<UUID>();

  // Key to indicate if a universe cert is hot reloadable
  public static final String KEY_CERT_HOT_RELOADABLE = "cert_hot_reloadable";

  public static Universe getOrBadRequest(UUID universeUUID, Customer customer) {
    Universe universe = getOrBadRequest(universeUUID);
    MDC.put("universe-id", universeUUID.toString());
    MDC.put("cluster-id", universeUUID.toString());
    if (!universe.getCustomerId().equals(customer.getId())) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Universe %s doesn't belong to Customer %s", universeUUID, customer.getUuid()));
    }
    return universe;
  }

  public enum HelmLegacy {
    V3,
    V2TO3
  }

  // The universe UUID.
  @Id private UUID universeUUID;

  // The version number of the object. This is used to synchronize updates from multiple clients.
  @Constraints.Required
  @Column(nullable = false)
  private int version;

  // Tracks when the universe was created.
  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date creationDate;

  // The universe name.
  private String name;

  // The customer id, needed only to enforce unique universe names for a customer.
  @Constraints.Required private Long customerId;

  @DbJson
  @Column(columnDefinition = "TEXT")
  private Map<String, String> config;

  private Boolean swamperConfigWritten;

  @JsonIgnore
  public void setConfig(Map<String, String> newConfig) {
    LOG.info(
        "Setting config {} on universe {} [ {} ]",
        Json.toJson(newConfig),
        getName(),
        getUniverseUUID());
    this.config = newConfig;
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
    universeDetailsJson = Json.stringify(Json.toJson(details));
    universeDetails = details;
  }

  public void resetVersion() {
    this.setVersion(-1);
    this.update();
  }

  @OneToMany(mappedBy = "universe", cascade = CascadeType.ALL, orphanRemoval = true)
  @JsonManagedReference
  private List<PitrConfig> pitrConfigs;

  @JsonIgnore
  public List<String> getVersions() {
    if (null == universeDetails || null == universeDetails.clusters) {
      return new ArrayList<>();
    }
    return universeDetails.clusters.stream()
        .filter(c -> c != null && c.userIntent != null)
        .map(c -> c.userIntent.ybSoftwareVersion)
        .collect(Collectors.toList());
  }

  @Transactional(isolation = TxIsolation.REPEATABLE_READ)
  @Override
  public boolean delete() {
    // Delete xCluster configs without universes.
    XClusterConfig.getByUniverseUuid(getUniverseUUID()).stream()
        .filter(
            xClusterConfig -> {
              if (xClusterConfig.getSourceUniverseUUID() == null) {
                return true;
              } else {
                if (getUniverseUUID().equals(xClusterConfig.getSourceUniverseUUID())) {
                  return xClusterConfig.getTargetUniverseUUID() == null;
                }
                return false;
              }
            })
        .forEach(
            xClusterConfig -> {
              // Delete DR configs with no xCluster configs.
              DrConfig drConfig = xClusterConfig.getDrConfig();
              if (Objects.nonNull(drConfig) && drConfig.getXClusterConfigs().size() == 1) {
                drConfig.delete();
              }
              xClusterConfig.delete();
            });
    return super.delete();
  }

  public static final Finder<UUID, Universe> find = new Finder<UUID, Universe>(Universe.class) {};

  // Prefix added to read only node.
  public static final String READONLY = "-readonly";

  // Prefix added to addon node.
  public static final String ADDON = "-addon";

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
    universe.setUniverseUUID(taskParams.getUniverseUUID());
    // Set the version of the object to 1.
    universe.setVersion(1);
    // Set the creation date.
    universe.setCreationDate(new Date());
    // Set the universe name.
    universe.setName(taskParams.getPrimaryCluster().userIntent.universeName);
    // Set the customer id.
    universe.setCustomerId(customerId);
    // Create the default universe details. This should be updated after creation.
    universe.universeDetails = taskParams;
    universe.universeDetailsJson =
        Json.stringify(
            RedactingService.filterSecretFields(
                Json.toJson(universe.universeDetails), RedactionTarget.APIS));
    universe.swamperConfigWritten = true;
    LOG.info(
        "Created db entry for universe {} [{}]", universe.getName(), universe.getUniverseUUID());
    LOG.debug(
        "Details for universe {} [{}] : [{}].",
        universe.getName(),
        universe.getUniverseUUID(),
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
    List<UUID> universeList = find.query().where().eq("customer_id", customer.getId()).findIds();
    Set<UUID> universeUUIDs = new HashSet<UUID>(universeList);
    return universeUUIDs;
  }

  public static Set<UUID> getAllUUIDs() {
    return ImmutableSet.copyOf(find.query().where().findIds());
  }

  /**
   * Fetches the universe UUIDs associated with customer IDs.
   *
   * @return map of customer ID to a set of its universe UUIDs.
   */
  public static Map<Long, Set<UUID>> getAllCustomerUniverseUUIDs() {
    return find.query().select("customerId, universeUUID").findList().stream()
        .collect(
            Collectors.groupingBy(
                u -> u.getCustomerId(),
                Collectors.mapping(Universe::getUniverseUUID, Collectors.toSet())));
  }

  public static Set<Universe> getAllWithoutResources() {
    List<Universe> rawList = find.query().findList();
    return rawList.stream().peek(Universe::fillUniverseDetails).collect(Collectors.toSet());
  }

  public static Set<Universe> getAllWithoutResources(Customer customer) {
    return getAllWithoutResources(customer, null);
  }

  public static Set<Universe> getAllWithoutResources(Customer customer, UUID uuid) {
    ExpressionList<Universe> query = find.query().where().eq("customer_id", customer.getId());
    if (uuid != null) {
      query.idEq(uuid);
    }
    List<Universe> rawList = query.findList();
    return rawList.stream().peek(Universe::fillUniverseDetails).collect(Collectors.toSet());
  }

  public static Set<Universe> getAllWithoutResources(Collection<UUID> uuids) {
    ExpressionList<Universe> query = find.query().where();
    CommonUtils.appendInClause(query, "universeUUID", uuids);
    List<Universe> rawList = query.findList();
    return rawList.stream().peek(Universe::fillUniverseDetails).collect(Collectors.toSet());
  }

  public static Set<Universe> getUniversesForSwamperConfigUpdate() {
    List<Universe> rawList = find.query().where().eq("swamperConfigWritten", false).findList();
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
    return universeUUIDs.stream()
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
   * Find a single attribute from universe_details_json column of Universe.
   *
   * @param clazz the attribute type.
   * @param universeUUID the universe UUID to be searched for.
   * @param fieldName the name of the field.
   * @return the attribute value.
   */
  public static <T> Optional<T> getUniverseDetailsField(
      Class<T> clazz, UUID universeUUID, String fieldName) {
    String query =
        String.format(
            "select universe_details_json::jsonb->>'%s' as field from universe"
                + " where universe_uuid = :universeUUID",
            fieldName);
    SqlQuery sqlQuery = DB.sqlQuery(query);
    sqlQuery.setParameter("universeUUID", universeUUID);
    return sqlQuery.findOneOrEmpty().map(row -> clazz.cast(row.get("field")));
  }

  /**
   * Find a single attribute from universe_details_json column of all Universe records.
   *
   * @param clazz the attribute type.
   * @param customerId the customer ID primary key.
   * @param fieldName the name of the field.
   * @return the attribute values for all universes.
   */
  public static <T> Map<UUID, T> getUniverseDetailsFields(
      Class<T> clazz, Long customerId, String fieldName) {
    String query =
        String.format(
            "select universe_uuid, universe_details_json::jsonb->>'%s' as field from universe"
                + " where customer_id = :customerId",
            fieldName);
    SqlQuery sqlQuery = DB.sqlQuery(query);
    sqlQuery.setParameter("customerId", customerId);
    return sqlQuery.findList().stream()
        .filter(r -> r.get("field") != null && clazz.isAssignableFrom(r.get("field").getClass()))
        .collect(
            Collectors.toMap(r -> (UUID) r.get("universe_uuid"), r -> clazz.cast(r.get("field"))));
  }

  /**
   * Interface using which we specify a callback to update the universe object. This is passed into
   * the save method.
   */
  public interface UniverseUpdater {
    void run(Universe universe);

    // Returns the config associated with this updater.
    default UniverseUpdaterConfig getConfig() {
      return UniverseUpdaterConfig.builder().build();
    }
  }

  /** Config parameters for the universe updater. */
  @Builder
  @Getter
  public static class UniverseUpdaterConfig {
    private boolean checkSuccess;
    private boolean forceUpdate;
    @Builder.Default private boolean freezeUniverse = true;
    private boolean ignoreAbsence;
    private Consumer<Universe> callback;
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
    UNIVERSE_KEY_LOCK.acquireLock(universeUUID);
    try {
      // Perform the below code block in transaction.
      AtomicReference<Universe> universeRef = new AtomicReference<>();
      TransactionUtil.doInTxn(
          () -> {
            Universe universe = Universe.getOrBadRequest(universeUUID);
            // Update the universe object which is supplied as a lambda function.
            // The lambda function can have DB changes.
            updater.run(universe);
            universe.save(incrementVersion);
            universeRef.set(universe);
          },
          TransactionUtil.DEFAULT_RETRY_CONFIG);
      return universeRef.get();
    } finally {
      UNIVERSE_KEY_LOCK.releaseLock(universeUUID);
    }
  }

  /**
   * Deletes the universe entry with the given UUID.
   *
   * @param universeUUID : uuid of the universe.
   */
  public static void delete(UUID universeUUID) {
    // First get the universe.
    Universe universe = Universe.getOrBadRequest(universeUUID);
    // Delete the universe.
    LOG.info("Deleting universe " + universe.getName() + ":" + universeUUID);
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
   * Checks if all nodes in universe have the node state 'Live'
   *
   * @return true if all nodes are in LIVE state
   */
  public boolean allNodesLive() {
    return getNodes().stream()
        .allMatch(nodeDetails -> nodeDetails.state.equals(NodeDetails.NodeState.Live));
  }

  /**
   * Checks if there is any node in a transit state across the universe.
   *
   * @return true if there is any such node.
   */
  public boolean nodesInTransit() {
    return nodesInTransit(null);
  }

  public boolean nodesInTransit(NodeDetails.NodeState omittedState) {
    return getNodes().stream().anyMatch(n -> n.isInTransit(omittedState));
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
      if (node.cloudInfo != null
          && StringUtils.isNotBlank(node.cloudInfo.private_ip)
          && node.cloudInfo.private_ip.equals(nodeIP)) {
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
   * Return the list of TServers in the primary cluster for this universe. E.g. the TServers in a
   * read replica will not be included.
   *
   * @return a list of TServers nodes
   */
  public List<NodeDetails> getTServersInPrimaryCluster() {
    List<NodeDetails> servers = getServers(ServerType.TSERVER);
    Collection<NodeDetails> primaryNodes =
        getNodesInCluster(getUniverseDetails().getPrimaryCluster().uuid);
    return servers.stream()
        .filter(server -> primaryNodes.contains(server))
        .collect(Collectors.toList());
  }

  /**
   * Return the list of live TServers in the primary cluster. TODO: junit tests for this
   * functionality (UniverseTest.java)
   *
   * @return a list of TServer nodes
   */
  public List<NodeDetails> getLiveTServersInPrimaryCluster() {
    List<NodeDetails> servers = getTServersInPrimaryCluster();
    List<NodeDetails> filteredServers =
        servers.stream()
            .filter(nodeDetails -> nodeDetails.state.equals(NodeDetails.NodeState.Live))
            .collect(Collectors.toList());

    if (filteredServers.isEmpty()) {
      LOG.trace(
          "No live nodes for getLiveTServersInPrimaryCluster in universe {}", getUniverseUUID());
    }
    return filteredServers;
  }

  public List<NodeDetails> getRunningTserversInPrimaryCluster() {
    List<NodeDetails> servers = getTServersInPrimaryCluster();
    List<NodeDetails> filteredServers =
        servers.stream().filter(NodeDetails::isConsideredRunning).collect(Collectors.toList());

    if (filteredServers.isEmpty()) {
      LOG.trace(
          "No Running nodes for getRunningTserversInPrimaryCluster in universe {}",
          getUniverseUUID());
    }
    return filteredServers;
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
        details.nodeDetailsSet.stream()
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

  public String getMasterAddresses(boolean mastersQueryable) {
    return getMasterAddresses(mastersQueryable, false);
  }

  /**
   * Returns a comma separated list of <privateIp:masterRpcPort> for all nodes that have the
   * isMaster flag set to true in this cluster.
   *
   * @param mastersQueryable Set to true if caller wants masters to be queryable, else false.
   * @return a comma separated string of master 'host:port' or, if masters are not queryable, an
   *     empty string.
   */
  public String getMasterAddresses(boolean mastersQueryable, boolean getSecondary) {
    List<NodeDetails> masters = getMasters();
    if (mastersQueryable && !verifyMastersAreQueryable(masters)) {
      return "";
    }
    return getHostPortsString(masters, ServerType.MASTER, PortType.RPC, getSecondary);
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
   * It returns a comma separated list of <privateIp:tserverHTTPPort> for all tservers in the
   * primary cluster of this universe.
   *
   * @return A comma separated string of 'host:port'
   */
  public String getTserverAddresses() {
    return getHostPortsString(getTServersInPrimaryCluster(), ServerType.TSERVER, PortType.RPC);
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
      return CertificateInfo.get(details.rootCA).getCertificate();
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
      if (details.rootAndClientRootCASame && details.rootCA != null) {
        return CertificateInfo.get(details.rootCA).getCertificate();
      }
      return CertificateInfo.get(details.getClientRootCA()).getCertificate();
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

  private String getHostPortsString(
      List<NodeDetails> serverNodes, ServerType type, PortType portType) {
    return getHostPortsString(serverNodes, type, portType, false);
  }

  // Helper API to create the based on the server type.
  private String getHostPortsString(
      List<NodeDetails> serverNodes, ServerType type, PortType portType, boolean getSecondary) {
    StringBuilder servers = new StringBuilder();
    for (NodeDetails node : serverNodes) {
      // Only get secondary if dual net legacy is false.
      boolean shouldGetSecondary =
          this.getConfig().getOrDefault(DUAL_NET_LEGACY, "true").equals("false") && getSecondary;
      String nodeIp =
          shouldGetSecondary ? node.cloudInfo.secondary_private_ip : node.cloudInfo.private_ip;
      // In case the secondary IP is null, just re-assign to primary.
      if (nodeIp == null || nodeIp.equals("null")) {
        nodeIp = node.cloudInfo.private_ip;
      }
      if (nodeIp != null) {
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
        servers.append(nodeIp).append(":").append(port);
      }
    }
    return servers.toString();
  }

  /**
   * Saves the universe to DB.
   *
   * @param incrementVersion the version is incremented if it is set.
   */
  public void save(boolean incrementVersion) {
    // Update the universe details json.
    this.universeDetailsJson =
        Json.stringify(
            RedactingService.filterSecretFields(
                Json.toJson(universeDetails), RedactionTarget.APIS));
    this.setVersion(incrementVersion ? this.getVersion() + 1 : this.getVersion());
    super.save();
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
   * Get deployment mode of node (on-prem/kubernetes/cloud provider)
   *
   * @param node - node to get info on
   * @return Get deployment details
   */
  public Common.CloudType getNodeDeploymentMode(NodeDetails node) {
    if (node == null) {
      throw new RuntimeException("node must be nonnull");
    }
    UniverseDefinitionTaskParams.Cluster cluster =
        getUniverseDetails().getClusterByUuid(node.placementUuid);
    return cluster.userIntent.providerType;
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
  @JsonIgnore
  public HostAndPort getMasterLeader() {
    final String masterAddresses = getMasterAddresses();
    final String cert = getCertificateNodetoNode();
    final YBClientService ybService =
        StaticInjectorHolder.injector().instanceOf(YBClientService.class);
    final YBClient client = ybService.getClient(masterAddresses, cert);
    try {
      return client.getLeaderMasterHostAndPort();
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
  }

  /**
   * Find the current master leader node. Can return null if master leader is missing. Note that the
   * master leader node may be a standalone master without tserver, such as is the case for K8s
   * universes.
   *
   * @return NodeDetails of the master leader
   */
  @JsonIgnore
  public NodeDetails getMasterLeaderNode() {
    return getNodeByPrivateIP(getMasterLeaderHostText());
  }

  /**
   * Find the current master leader in the universe
   *
   * @return a String of the private_ip of the current master leader in the universe or an empty
   *     string if not found
   */
  @JsonIgnore
  public String getMasterLeaderHostText() {
    final HostAndPort masterLeader = getMasterLeader();
    if (masterLeader == null) return "";
    return masterLeader.getHost();
  }

  public boolean universeIsLocked() {
    return getUniverseDetails().updateInProgress;
  }

  public boolean isYbcEnabled() {
    return getUniverseDetails().isYbcInstalled();
  }

  public boolean nodeExists(String host, int port) {
    return getUniverseDetails()
        .nodeDetailsSet
        .parallelStream()
        .anyMatch(
            n ->
                n.cloudInfo.private_ip != null
                    && n.cloudInfo.private_ip.equals(host)
                    && (port == n.masterHttpPort
                        || port == n.tserverHttpPort
                        || port == n.ysqlServerHttpPort
                        || port == n.yqlServerHttpPort
                        || port == n.redisServerHttpPort
                        || port == n.nodeExporterPort));
  }

  public void incrementVersion() {
    Universe.saveDetails(getUniverseUUID(), ignoreUniverse -> {});
  }

  public static Set<Universe> universeDetailsIfCertsExists(UUID certUUID, UUID customerUUID) {
    return Customer.get(customerUUID).getUniverses().stream()
        .filter(
            s ->
                (s.getUniverseDetails().rootCA != null
                        && s.getUniverseDetails().rootCA.equals(certUUID))
                    || (s.getUniverseDetails().getClientRootCA() != null
                        && s.getUniverseDetails().getClientRootCA().equals(certUUID)))
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

  static Set<UUID> getUniverseUUIDsForCustomer(Long customerId) {
    return find.query().select("universeUUID").where().eq("customer_id", customerId).findList()
        .stream()
        .map(Universe::getUniverseUUID)
        .collect(Collectors.toSet());
  }

  static Set<Universe> getUniversesForCustomer(Long customerId) {
    return find.query().where().eq("customer_id", customerId).findSet().stream()
        .peek(Universe::fillUniverseDetails)
        .collect(Collectors.toSet());
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

  // Allow https when software version given is >= 2.17.1.0-b14 and isNodeUIHttpsEnabled is true.
  // Invalid software versions will not allow https.
  // compareYbVersions() returns 0 if incorrect software version is passed, hence the strictly
  // greater.
  public static boolean shouldEnableHttpsUI(
      boolean enableNodeToNodeEncrypt, String ybSoftwareVersion, boolean isNodeUIHttpsEnabled) {
    return isNodeUIHttpsEnabled
        && enableNodeToNodeEncrypt
        && (Util.compareYbVersions(ybSoftwareVersion, "2.17.1.0-b13", true) > 0);
  }

  public Optional<TaskInfo> maybeGetLastTaskInfo() {
    if (getUniverseDetails().updatingTaskUUID != null
        && getUniverseDetails().updatingTask != null) {
      return TaskInfo.maybeGet(getUniverseDetails().updatingTaskUUID);
    }
    return Optional.empty();
  }

  @PostRemove
  public void cleanupUniverse() {
    RoleBindingUtil.cleanupRoleBindings(ResourceType.UNIVERSE, this.getUniverseUUID());
  }
}
