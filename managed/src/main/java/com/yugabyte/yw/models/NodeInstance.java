// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.gdata.util.common.base.Preconditions;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.ebean.DB;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.Query;
import io.ebean.RawSql;
import io.ebean.RawSqlBuilder;
import io.ebean.SqlUpdate;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.AccessLevel;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@ApiModel(description = "A single node instance, attached to a provider and availability zone")
public class NodeInstance extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(NodeInstance.class);
  private static final Map<UUID, Set<InflightNodeInstanceInfo>> CLUSTER_INFLIGHT_NODE_INSTANCES =
      new ConcurrentHashMap<>();

  @Data
  @EqualsAndHashCode(onlyExplicitlyIncluded = true)
  static class InflightNodeInstanceInfo {
    private final String nodeName;
    private final UUID zoneUuid;
    @EqualsAndHashCode.Include private final UUID nodeUuid;

    InflightNodeInstanceInfo(String nodeName, UUID nodeUuid, UUID zoneUuid) {
      this.nodeName = nodeName;
      this.nodeUuid = nodeUuid;
      this.zoneUuid = zoneUuid;
    }
  }

  @Builder
  @Data
  @AllArgsConstructor
  @NoArgsConstructor
  public static class UniverseMetadata {
    private boolean useSystemd = true;
    private boolean assignStaticPublicIp = false;
    private boolean otelCollectorEnabled = false;
  }

  public enum State {
    @EnumValue("DECOMMISSIONED")
    DECOMMISSIONED,
    @EnumValue("USED")
    USED,
    @EnumValue("FREE")
    FREE
  }

  @Id
  @ApiModelProperty(value = "The node's UUID", accessMode = READ_ONLY)
  private UUID nodeUuid;

  @Column
  @ApiModelProperty(value = "The node's type code", example = "c5large")
  private String instanceTypeCode;

  @Column(nullable = false)
  @ApiModelProperty(value = "The node's name", example = "India node")
  private String nodeName;

  @Column(nullable = false)
  @ApiModelProperty(value = "The node instance's name", example = "Mumbai instance")
  private String instanceName;

  @Column(nullable = false)
  @ApiModelProperty(value = "The availability zone's UUID")
  private UUID zoneUuid;

  @Column(nullable = false)
  @ApiModelProperty(value = "State of on-prem node", accessMode = READ_ONLY)
  @Enumerated(EnumType.STRING)
  private State state;

  @DbJson @JsonIgnore private UniverseMetadata universeMetadata;

  @Getter(AccessLevel.NONE)
  @Setter(AccessLevel.NONE)
  @Column(nullable = false)
  @ApiModelProperty(value = "Node details (as a JSON object)")
  private String nodeDetailsJson;

  @Getter(AccessLevel.NONE)
  @Setter(AccessLevel.NONE)
  // Preserving the details into a structured class.
  @ApiModelProperty(value = "Node details")
  private NodeInstanceData nodeDetails;

  public void setDetails(NodeInstanceData details) {
    this.nodeDetails = details;
    this.nodeDetailsJson = Json.stringify(Json.toJson(this.nodeDetails));
  }

  public NodeInstanceData getDetails() {
    if (nodeDetails == null) {
      nodeDetails = Json.fromJson(Json.parse(nodeDetailsJson), NodeInstanceData.class);
    }
    return nodeDetails;
  }

  @JsonProperty
  public String getNodeName() {
    return nodeName;
  }

  @JsonIgnore
  public void setNodeName(String name) {
    nodeName = name;
    // This parses the JSON if first time accessing details.
    NodeInstanceData details = getDetails();
    details.nodeName = name;
    setDetails(details);
  }

  // Method sets node name to empty string and state to FREE and persists the value.
  public void clearNodeDetails() {
    this.setState(State.FREE);
    this.setNodeName("");
    this.universeMetadata = null;
    this.save();
  }

  @JsonIgnore
  public void setToFailedCleanup(Universe universe, NodeDetails node) {
    this.setState(State.DECOMMISSIONED);
    this.setNodeName("");
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    this.setUniverseMetadata(
        UniverseMetadata.builder()
            .useSystemd(universeDetails.getPrimaryCluster().userIntent.useSystemd)
            .assignStaticPublicIp(
                universeDetails.getPrimaryCluster().userIntent.assignStaticPublicIP)
            .otelCollectorEnabled(universeDetails.otelCollectorEnabled)
            .build());
    this.save();
  }

  @ApiModelProperty(
      value = "Node details (as a JSON object)",
      example = "{\"ip\":\"1.1.1.1\",\"sshUser\":\"centos\"}")
  public String getDetailsJson() {
    return nodeDetailsJson;
  }

  public static final Finder<UUID, NodeInstance> find =
      new Finder<UUID, NodeInstance>(NodeInstance.class) {};

  public static List<NodeInstance> listByZone(UUID zoneUuid, String instanceTypeCode) {
    return listByZone(zoneUuid, instanceTypeCode, null);
  }

  public static List<NodeInstance> listByZone(
      UUID zoneUuid, String instanceTypeCode, Set<UUID> nodeUuids) {
    // Search in the proper AZ and only for nodes not in use.
    ExpressionList<NodeInstance> exp =
        NodeInstance.find.query().where().eq("zone_uuid", zoneUuid).eq("state", State.FREE);
    // Filter by instance type if asked to.
    if (instanceTypeCode != null) {
      exp.where().eq("instance_type_code", instanceTypeCode);
    }
    if (CollectionUtils.isNotEmpty(nodeUuids)) {
      exp = appendInClause(NodeInstance.find.query().where(), "node_uuid", nodeUuids);
    }
    return exp.findList();
  }

  public static List<NodeInstance> listByRegion(UUID regionUUID, String instanceTypeCode) {
    Region region = Region.getOrBadRequest(regionUUID);
    List<UUID> azUUIDs =
        region.getZones().stream().map(az -> az.getUuid()).collect(Collectors.toList());
    List<NodeInstance> nodes = null;
    // Search in the proper AZ.
    ExpressionList<NodeInstance> exp = NodeInstance.find.query().where().in("zone_uuid", azUUIDs);
    // Search only for nodes not in use.
    exp.where().eq("state", State.FREE);
    // Filter by instance type if asked to.
    if (instanceTypeCode != null) {
      exp.where().eq("instance_type_code", instanceTypeCode);
    }
    nodes = exp.findList();
    return nodes;
  }

  public static List<NodeInstance> listByProvider(UUID providerUUID) {
    String nodeQuery =
        "select DISTINCT n.* from node_instance n, availability_zone az, region r, provider p "
            + " where n.zone_uuid = az.uuid and az.region_uuid = r.uuid and r.provider_uuid = "
            + "'"
            + providerUUID
            + "'";
    RawSql rawSql =
        RawSqlBuilder.unparsed(nodeQuery).columnMapping("node_uuid", "nodeUuid").create();
    Query<NodeInstance> query = DB.find(NodeInstance.class);
    query.setRawSql(rawSql);
    List<NodeInstance> list = query.findList();
    return list;
  }

  public static List<NodeInstance> listByCustomer(UUID customerUUID) {
    String nodeQuery =
        "select DISTINCT n.*"
            + " from node_instance n, availability_zone az, region r, provider p, customer c"
            + " where n.zone_uuid = az.uuid and az.region_uuid = r.uuid and"
            + " r.provider_uuid = p.uuid and c.uuid = '"
            + customerUUID
            + "'";
    RawSql rawSql =
        RawSqlBuilder.unparsed(nodeQuery).columnMapping("node_uuid", "nodeUuid").create();
    Query<NodeInstance> query = DB.find(NodeInstance.class);
    query.setRawSql(rawSql);
    List<NodeInstance> list = query.findList();
    return list;
  }

  public static List<NodeInstance> listByUniverse(UUID universeUUID) {
    Optional<Universe> optUniverse = Universe.maybeGet(universeUUID);
    if (!optUniverse.isPresent()) {
      return Collections.emptyList();
    }
    Universe universe = optUniverse.get();
    UUID providerUUID =
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    Set<UUID> nodeUUIDS =
        universe.getNodes().stream().map(NodeDetails::getNodeUuid).collect(Collectors.toSet());
    List<NodeInstance> nodeInstances = NodeInstance.listByProvider(providerUUID);
    List<NodeInstance> filteredInstances =
        nodeInstances.stream()
            .filter(instance -> nodeUUIDS.contains(instance.getNodeUuid()))
            .collect(Collectors.toList());

    return filteredInstances;
  }

  public static int deleteByProvider(UUID providerUUID) {
    String deleteNodeQuery =
        "delete from node_instance where zone_uuid in (select az.uuid from availability_zone az"
            + " join region r on az.region_uuid = r.uuid and r.provider_uuid=:provider_uuid)";
    SqlUpdate deleteStmt = DB.sqlUpdate(deleteNodeQuery);
    deleteStmt.setParameter("provider_uuid", providerUUID);
    return deleteStmt.execute();
  }

  private static List<NodeInstance> filterInflightNodeUuids(List<NodeInstance> nodes) {
    Set<UUID> inflightNodeUuids =
        CLUSTER_INFLIGHT_NODE_INSTANCES.values().stream()
            .flatMap(Set::stream)
            .map(InflightNodeInstanceInfo::getNodeUuid)
            .collect(Collectors.toSet());
    return nodes.stream()
        .filter(n -> !inflightNodeUuids.contains(n.getNodeUuid()))
        .collect(Collectors.toList());
  }

  /**
   * Reserve nodes in memory without persisting the changes in the database.
   *
   * @param clusterUuid Cluster UUID.
   * @param onpremAzToNodes AZ to set of node names.
   * @param instanceTypeCode instance code.
   * @return map of node name to node instance.
   */
  public static synchronized Map<String, NodeInstance> reserveNodes(
      UUID clusterUuid, Map<UUID, Set<String>> onpremAzToNodes, String instanceTypeCode) {
    Preconditions.checkState(
        !CLUSTER_INFLIGHT_NODE_INSTANCES.containsKey(clusterUuid),
        "Nodes already reserved for cluster " + clusterUuid);
    Map<String, NodeInstance> outputMap = new HashMap<>();
    try {
      for (Entry<UUID, Set<String>> entry : onpremAzToNodes.entrySet()) {
        UUID zoneUuid = entry.getKey();
        Set<String> nodeNames = entry.getValue();
        List<NodeInstance> nodes = filterInflightNodeUuids(listByZone(zoneUuid, instanceTypeCode));
        if (nodes.size() < nodeNames.size()) {
          LOG.error(
              "AZ {} has {} nodes of instance type {} but needs {}.",
              zoneUuid,
              nodes.size(),
              instanceTypeCode,
              nodeNames.size());
          throw new RuntimeException("Not enough nodes in AZ " + zoneUuid);
        }
        Set<InflightNodeInstanceInfo> instanceInfos =
            CLUSTER_INFLIGHT_NODE_INSTANCES.computeIfAbsent(clusterUuid, k -> new HashSet<>());
        int index = 0;
        for (String nodeName : nodeNames) {
          NodeInstance node = nodes.get(index);
          outputMap.put(nodeName, node);
          instanceInfos.add(new InflightNodeInstanceInfo(nodeName, node.getNodeUuid(), zoneUuid));
          index++;
          LOG.info(
              "Marking node {} (ip {}) as inflight", node.getInstanceName(), node.getDetails().ip);
        }
      }
    } catch (RuntimeException e) {
      CLUSTER_INFLIGHT_NODE_INSTANCES.remove(clusterUuid);
      outputMap.clear();
      throw e;
    }
    return outputMap;
  }

  /**
   * Release the reserved uncommitted nodes.
   *
   * @param clusterUuid
   */
  public static void releaseReservedNodes(UUID clusterUuid) {
    CLUSTER_INFLIGHT_NODE_INSTANCES.remove(clusterUuid);
  }

  /**
   * Commit the reserved nodes in memory to the database. The reserved nodes are cleared when this
   * method returns. It is the responsibility of the caller to call this method in transaction.
   *
   * @param clusterUuid the cluster UUID.
   * @return the previously reserved node instances.
   */
  public static synchronized Map<String, NodeInstance> commitReservedNodes(UUID clusterUuid) {
    Set<InflightNodeInstanceInfo> instanceInfos = CLUSTER_INFLIGHT_NODE_INSTANCES.get(clusterUuid);
    Preconditions.checkState(
        !CollectionUtils.isEmpty(instanceInfos),
        "No nodes are reserved for cluster " + clusterUuid);
    Map<String, NodeInstance> outputMap = new HashMap<>();
    try {
      Map<UUID, List<InflightNodeInstanceInfo>> azInstanceInfos =
          instanceInfos.stream()
              .collect(Collectors.groupingBy(InflightNodeInstanceInfo::getZoneUuid));
      for (Map.Entry<UUID, List<InflightNodeInstanceInfo>> entry : azInstanceInfos.entrySet()) {
        UUID zoneUuid = entry.getKey();
        Map<UUID, InflightNodeInstanceInfo> nodeUuidInstanceInfoMap =
            entry.getValue().stream()
                .collect(
                    Collectors.toMap(InflightNodeInstanceInfo::getNodeUuid, Function.identity()));
        List<NodeInstance> nodes = listByZone(zoneUuid, null, nodeUuidInstanceInfoMap.keySet());
        // Ensure that the nodes are still available.
        Preconditions.checkState(
            nodes.size() == nodeUuidInstanceInfoMap.keySet().size(),
            "Unexpected error in verifying the count for node instance for cluster " + clusterUuid);
        for (NodeInstance node : nodes) {
          InflightNodeInstanceInfo instanceInfo = nodeUuidInstanceInfoMap.get(node.getNodeUuid());
          node.setState(State.USED);
          node.setNodeName(instanceInfo.getNodeName());
          outputMap.put(instanceInfo.getNodeName(), node);
          LOG.info(
              "Marking node {} (ip {}) as in-use.",
              instanceInfo.getNodeName(),
              node.getDetails().ip);
        }
      }
      // All good, save to DB.
      for (NodeInstance node : outputMap.values()) {
        node.save();
      }
    } finally {
      CLUSTER_INFLIGHT_NODE_INSTANCES.remove(clusterUuid);
    }
    return outputMap;
  }

  /**
   * Pick available nodes in zones specified by onpremAzToNodes with with the instance type
   * specified
   */
  public static synchronized Map<String, NodeInstance> pickNodes(
      UUID clusterUuid, Map<UUID, Set<String>> onpremAzToNodes, String instanceTypeCode) {
    reserveNodes(clusterUuid, onpremAzToNodes, instanceTypeCode);
    return commitReservedNodes(clusterUuid);
  }

  @Deprecated
  public static NodeInstance get(UUID nodeUuid) {
    NodeInstance node = NodeInstance.find.byId(nodeUuid);
    return node;
  }

  public static Optional<NodeInstance> maybeGet(UUID nodeUuid) {
    return Optional.ofNullable(get(nodeUuid));
  }

  public static NodeInstance getOrBadRequest(UUID nodeUuid) {
    NodeInstance node = get(nodeUuid);
    if (node == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid node UUID: " + nodeUuid);
    }
    return node;
  }

  // TODO: this is a temporary hack until we manage to plumb through the node UUID through the task
  // framework.
  public static NodeInstance getByName(String name) {
    return maybeGetByName(name)
        .orElseThrow(() -> new RuntimeException("Expecting to find a node with name: " + name));
  }

  public static Optional<NodeInstance> maybeGetByName(String name) {
    List<NodeInstance> nodes = NodeInstance.find.query().where().eq("node_name", name).findList();
    if (CollectionUtils.isEmpty(nodes)) {
      return Optional.empty();
    }
    if (nodes.size() > 1) {
      throw new RuntimeException("Expecting to find a single node with name: " + name);
    }
    return Optional.of(nodes.get(0));
  }

  public static List<NodeInstance> listByUuids(Collection<UUID> nodeUuids) {
    if (CollectionUtils.isEmpty(nodeUuids)) {
      return Collections.emptyList();
    }
    return NodeInstance.find.query().where().in("nodeUuid", nodeUuids).findList();
  }

  public static List<NodeInstance> getAll() {
    return NodeInstance.find.all();
  }

  public static NodeInstance create(UUID zoneUuid, NodeInstanceData formData) {
    NodeInstance node = new NodeInstance();
    node.zoneUuid = zoneUuid;
    node.instanceTypeCode = formData.instanceType;
    String instanceName = formData.instanceName;
    if (instanceName == null) instanceName = "";
    node.instanceName = instanceName;
    node.setDetails(formData);
    node.setNodeName("");
    node.setState(State.FREE);
    node.save();
    return node;
  }

  public static boolean checkIpInUse(String ipAddress) {
    List<NodeInstance> nodeList = NodeInstance.find.all();
    return nodeList.stream().anyMatch(x -> x.getDetails().ip.equals(ipAddress));
  }
}
