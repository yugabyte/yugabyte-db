// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.yugabyte.yw.forms.NodeInstanceFormData;

import com.avaje.ebean.Query;
import com.avaje.ebean.Model;
import com.avaje.ebean.ExpressionList;
import com.avaje.ebean.RawSqlBuilder;
import com.avaje.ebean.RawSql;
import com.avaje.ebean.Ebean;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import javax.persistence.Id;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.data.validation.Constraints;
import play.libs.Json;

@Entity
public class NodeInstance extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(NodeInstance.class);

  @Id
  public UUID nodeUuid;

  @Column
  public String instanceTypeCode;

  @Column(nullable = false)
  private String nodeName;
  public String getNodeName() { return nodeName; }
  public void setNodeName(String name) {
    nodeName = name;
    // This parses the JSON if first time accessing details.
    NodeInstanceFormData details = getDetails();
    details.nodeName = name;
    setDetails(details);
  }

  @Column(nullable = false)
  public UUID zoneUuid;

  @Column(nullable = false)
  public boolean inUse;

  @Column(nullable = false)
  private String nodeDetailsJson;

  // Preserving the details into a structured class.
  private NodeInstanceFormData nodeDetails;

  public void setDetails(NodeInstanceFormData details) {
    this.nodeDetails = details;
    this.nodeDetailsJson = Json.stringify(Json.toJson(this.nodeDetails));
  }
  public NodeInstanceFormData getDetails() {
    if (nodeDetails == null) {
      nodeDetails = Json.fromJson(
        Json.parse(nodeDetailsJson),
        NodeInstanceFormData.class);
    }
    return nodeDetails;
  }

  public String getDetailsJson() {
    return nodeDetailsJson;
  }

  public static final Find<UUID, NodeInstance> find = new Find<UUID, NodeInstance>(){};

  public static List<NodeInstance> listByZone(UUID zoneUuid, String instanceTypeCode) {
    List<NodeInstance> nodes = null;
    // Search in the proper AZ.
    ExpressionList<NodeInstance> exp = NodeInstance.find.where().eq("zone_uuid", zoneUuid);
    // Search only for nodes not in use.
    exp.where().eq("in_use", false);
    // Filter by instance type if asked to.
    if (instanceTypeCode != null) {
      exp.where().eq("instance_type_code", instanceTypeCode);
    }
    nodes = exp.findList();
    return nodes;
  }

  public static List<NodeInstance> listByProvider(UUID providerUUID) {
    String nodeQuery = "select DISTINCT n.*   from node_instance n, availability_zone az, region r, provider p " +
      " where n.zone_uuid = az.uuid and az.region_uuid = r.uuid and r.provider_uuid = " + "'"+ providerUUID + "'";
    RawSql rawSql = RawSqlBuilder.unparsed(nodeQuery).columnMapping("node_uuid",  "nodeUuid").create();
    Query<NodeInstance> query = Ebean.find(NodeInstance.class);
    query.setRawSql(rawSql);
    List<NodeInstance> list = query.findList();
    return list;
  }

  public static synchronized Map<String, NodeInstance> pickNodes(
    Map<UUID, List<String>> onpremAzToNodes, String instanceTypeCode) {
    Map<String, NodeInstance> outputMap = new HashMap<String, NodeInstance>();
    Throwable error = null;
    try {
      for (Entry<UUID, List<String>> entry : onpremAzToNodes.entrySet()) {
        UUID zoneUuid = entry.getKey();
        List<String> nodeNames = entry.getValue();
        List<NodeInstance> nodes = listByZone(zoneUuid, instanceTypeCode);
        if (nodes.size() < nodeNames.size()) {
          LOG.error("AZ {} has {} nodes of instance type {} but needs {}.",
            zoneUuid, nodes.size(), instanceTypeCode, nodeNames.size());
          throw new RuntimeException("Not enough nodes in AZ " + zoneUuid);
        }
        int index = 0;
        for (String nodeName : nodeNames) {
          NodeInstance node = nodes.get(index);
          node.inUse = true;
          node.setNodeName(nodeName);
          outputMap.put(nodeName, node);
          ++index;
        }
      }
      // All good, save to DB.
      for (NodeInstance node : outputMap.values()) {
        node.save();
      }
    } catch (Throwable t) {
      error = t;
      throw t;
    } finally {
      if (error != null) {
        outputMap = null;
        // TODO: any cleanup needed?
      }
    }
    return outputMap;
  }

  public static NodeInstance get(UUID nodeUuid) {
    NodeInstance node = NodeInstance.find.byId(nodeUuid);
    return node;
  }

  // TODO: this is a temporary hack until we manage to plumb through the node UUID through the task
  // framework.
  public static NodeInstance getByName(String name) {
    List<NodeInstance> nodes = NodeInstance.find.where().eq("node_name", name).findList();
    if (nodes == null || nodes.size() != 1) {
      throw new RuntimeException("Expecting to find a single node with name: " + name);
    }
    return nodes.get(0);
  }

  public static NodeInstance create(UUID zoneUuid, NodeInstanceFormData formData) {
    NodeInstance node = new NodeInstance();
    node.zoneUuid = zoneUuid;
    node.inUse = false;
    node.instanceTypeCode = formData.instanceType;
    node.setDetails(formData);
    node.setNodeName("");
    node.save();
    return node;
  }
}
