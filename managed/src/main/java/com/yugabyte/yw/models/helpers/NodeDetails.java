// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.common.NodeActionType.ADD;
import static com.yugabyte.yw.common.NodeActionType.DELETE;
import static com.yugabyte.yw.common.NodeActionType.QUERY;
import static com.yugabyte.yw.common.NodeActionType.RELEASE;
import static com.yugabyte.yw.common.NodeActionType.REMOVE;
import static com.yugabyte.yw.common.NodeActionType.START;
import static com.yugabyte.yw.common.NodeActionType.STOP;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.NodeActionType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;

/** Represents all the details of a cloud node that are of interest. */
@JsonIgnoreProperties(
    // Ignore auto-generated boolean properties: https://stackoverflow.com/questions/32270422
    value = {"master", "tserver", "redisServer", "yqlServer", "ysqlServer"},
    ignoreUnknown = true)
@ApiModel(description = "Details of a cloud node")
public class NodeDetails {
  // The id of the node. This is usually present in the node name.
  @ApiModelProperty(value = "Node ID")
  public int nodeIdx = -1;

  // Name of the node.
  @ApiModelProperty(value = "Node name")
  public String nodeName;

  // The UUID of the node we are using.
  // TODO: only used for onprem at the moment.
  @ApiModelProperty(value = "Node UUID")
  public UUID nodeUuid;

  // Information about the node that is returned by the cloud provider.
  @ApiModelProperty(value = "Node information, as reported by the cloud provider")
  public CloudSpecificInfo cloudInfo;

  // The AZ UUID (the YB UUID for the AZ) into which the node is deployed.
  @ApiModelProperty(value = "The availability zone's UUID")
  public UUID azUuid;

  // The UUID of the cluster that this node belongs to.
  @ApiModelProperty(value = "UUID of the cluster to which this node belongs")
  public UUID placementUuid;

  @ApiModelProperty(value = "Machine image name")
  public String machineImage;

  // Possible states in which this node can exist.
  public enum NodeState {
    // Set when a new node needs to be added into a Universe and has not yet been created.
    ToBeAdded(DELETE),
    // Set when a new node is provisioned and configured but before it is added into
    // the existing cluster.
    ToJoinCluster(REMOVE),
    // Set after the node (without any configuration) is created using the IaaS provider at the
    // end of the provision step.
    Provisioned(),
    // Set after the YB software installed and some basic configuration done on a provisioned node.
    SoftwareInstalled(START, DELETE),
    // Set after the YB software is upgraded via Rolling Restart.
    UpgradeSoftware(),
    // Set after the YB specific GFlags are updated via Rolling Restart.
    UpdateGFlags(),
    // Set after all the services (master, tserver, etc) on a node are successfully running.
    Live(STOP, REMOVE, QUERY),

    // Set when node is about to enter the stopped state.
    Stopping(),
    // Set when node is about to be set to live state.
    Starting(),
    // Set when node has been stopped and no longer has a master or a tserver running.
    Stopped(START, REMOVE, QUERY),
    // Set when node is unreachable but has not been Removed from the universe.
    Unreachable(),
    // Set when a node is marked for removal. Note that we will wait to get all its data out.
    ToBeRemoved(REMOVE),
    // Set just before sending the request to the IaaS provider to terminate this node.
    Removing(),
    // Set after the node has been removed.
    Removed(ADD, RELEASE),
    // Set when node is about to enter the Live state from Removed/Decommissioned state.
    Adding(DELETE),
    // Set when a stopped/removed node is about to enter the Decommissioned state.
    BeingDecommissioned(),
    // After a stopped/removed node is returned back to the IaaS.
    Decommissioned(ADD, DELETE),
    // Set when the cert is being updated.
    UpdateCert(),
    // Set when TLS params (node-to-node and client-to-node) is being toggled
    ToggleTls(),
    // Set when the node is being resized to a new intended type
    Resizing(),
    // Set when the node is being upgraded to systemd from cron
    SystemdUpgrade();

    private final NodeActionType[] allowedActions;

    NodeState(NodeActionType... allowedActions) {
      this.allowedActions = allowedActions;
    }

    public ImmutableSet<NodeActionType> allowedActions() {
      return ImmutableSet.copyOf(allowedActions);
    }
  }

  // The current state of the node.
  @ApiModelProperty(value = "Node state", example = "Provisioned")
  public NodeState state;

  // True if this node is a master, along with port info.
  @ApiModelProperty(value = "True if this node is a master")
  public boolean isMaster;

  @ApiModelProperty(value = "Master HTTP port")
  public int masterHttpPort = 7000;

  @ApiModelProperty(value = "Master RCP port")
  public int masterRpcPort = 7100;

  // True if this node is a tserver, along with port info.
  @ApiModelProperty(value = "True if this node is a Tablet server")
  public boolean isTserver = true;

  @ApiModelProperty(value = "Tablet server HTTP port")
  public int tserverHttpPort = 9000;

  @ApiModelProperty(value = "Tablet server RPC port")
  public int tserverRpcPort = 9100;

  // True if this node is a Redis server, along with port info.
  @ApiModelProperty(value = "True if this node is a REDIS server")
  public boolean isRedisServer = true;

  @ApiModelProperty(value = "REDIS HTTP port")
  public int redisServerHttpPort = 11000;

  @ApiModelProperty(value = "REDIS RPC port")
  public int redisServerRpcPort = 6379;

  // True if this node is a YSQL server, along with port info.
  @ApiModelProperty(value = "True if this node is a YCQL server")
  public boolean isYqlServer = true;

  @ApiModelProperty(value = "YCQL HTTP port")
  public int yqlServerHttpPort = 12000;

  @ApiModelProperty(value = "YCQL RPC port")
  public int yqlServerRpcPort = 9042;

  // True if this node is a YSQL server, along with port info.
  @ApiModelProperty(value = "True if this node is a YSQL server")
  public boolean isYsqlServer = true;

  @ApiModelProperty(value = "YSQL HTTP port")
  public int ysqlServerHttpPort = 13000;

  @ApiModelProperty(value = "YSQL RPC port")
  public int ysqlServerRpcPort = 5433;

  // Which port node_exporter is running on.
  @ApiModelProperty(value = "Node exporter port")
  public int nodeExporterPort = 9300;

  // True if cronjobs were properly configured for this node.
  @ApiModelProperty(value = "True if cron jobs were properly configured for this node")
  public boolean cronsActive = true;

  // List of states which are considered in-transit and ops such as upgrade should not be allowed.
  public static final Set<NodeState> IN_TRANSIT_STATES =
      ImmutableSet.of(
          NodeState.Removed,
          NodeState.Stopped,
          NodeState.Decommissioned,
          NodeState.Resizing,
          NodeState.SystemdUpgrade);

  @Override
  public NodeDetails clone() {
    NodeDetails clone = new NodeDetails();
    clone.isMaster = this.isMaster;
    clone.isTserver = this.isTserver;
    clone.isRedisServer = this.isRedisServer;
    clone.isYqlServer = this.isYqlServer;
    clone.isYsqlServer = this.isYsqlServer;
    clone.state = this.state;
    clone.azUuid = this.azUuid;
    clone.cloudInfo = this.cloudInfo.clone();
    clone.nodeName = this.nodeName;
    clone.nodeIdx = this.nodeIdx;
    clone.nodeUuid = this.nodeUuid;
    clone.placementUuid = this.placementUuid;
    clone.machineImage = this.machineImage;
    return clone;
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("name: ")
        .append(nodeName)
        .append(", ")
        .append(cloudInfo.toString())
        .append(", isMaster: ")
        .append(isMaster)
        .append(", isTserver: ")
        .append(isTserver)
        .append(", state: ")
        .append(state)
        .append(", azUuid: ")
        .append(azUuid)
        .append(", placementUuid: ")
        .append(placementUuid);
    return sb.toString();
  }

  @JsonIgnore
  public boolean isActive() {
    return !(state == NodeState.Unreachable
        || state == NodeState.ToBeRemoved
        || state == NodeState.Removing
        || state == NodeState.Removed
        || state == NodeState.Starting
        || state == NodeState.Stopped
        || state == NodeState.Adding
        || state == NodeState.BeingDecommissioned
        || state == NodeState.Decommissioned
        || state == NodeState.SystemdUpgrade);
  }

  @JsonIgnore
  public boolean isQueryable() {
    return (state == NodeState.UpgradeSoftware
        || state == NodeState.UpdateGFlags
        || state == NodeState.Live
        || state == NodeState.ToBeRemoved
        || state == NodeState.Removing
        || state == NodeState.Stopping
        || state == NodeState.UpdateCert
        || state == NodeState.ToggleTls);
  }

  @JsonIgnore
  public boolean isInTransit() {
    return IN_TRANSIT_STATES.contains(state);
  }

  @JsonIgnore
  public boolean isRemovable() {
    return state == NodeState.ToBeAdded
        || state == NodeState.Adding
        || state == NodeState.SoftwareInstalled
        || state == NodeState.Decommissioned;
  }

  @JsonIgnore
  public boolean isInPlacement(UUID placementUuid) {
    return this.placementUuid != null && this.placementUuid.equals(placementUuid);
  }

  @JsonIgnore
  public String getRegion() {
    return this.cloudInfo.region;
  }

  @JsonIgnore
  public String getZone() {
    return this.cloudInfo.az;
  }

  public int getNodeIdx() {
    return this.nodeIdx;
  }

  public UUID getAzUuid() {
    return azUuid;
  }
}
