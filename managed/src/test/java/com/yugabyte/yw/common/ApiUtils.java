// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import java.util.HashSet;
import java.util.LinkedList;

import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TableDetails;
import org.yb.ColumnSchema.SortOrder;

public class ApiUtils {
  public static Universe.UniverseUpdater mockUniverseUpdater() {
    return mockUniverseUpdater("host");
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(String nodePrefix) {
    return new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails.userIntent = new UserIntent();
        universeDetails.userIntent.accessKeyCode = "yugabyte-default";
        // Add a desired number of nodes.
        universeDetails.userIntent.numNodes = universeDetails.userIntent.replicationFactor;
        universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node = getDummyNodeDetails(idx, NodeDetails.NodeState.Running,
                                           idx <= universeDetails.userIntent.replicationFactor);
          universeDetails.nodeDetailsSet.add(node);
        }
        universeDetails.nodePrefix = nodePrefix;
        universe.setUniverseDetails(universeDetails);
      }
    };
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(UserIntent userIntent) {
    return new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails = new UniverseDefinitionTaskParams();
        universeDetails.userIntent = userIntent;
        universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node =
              getDummyNodeDetails(idx, NodeDetails.NodeState.Running,
                 idx <= universeDetails.userIntent.replicationFactor);
          universeDetails.nodeDetailsSet.add(node);
        }
        universeDetails.nodePrefix = "host";
        universe.setUniverseDetails(universeDetails);
      }
    };
  }

  public static Universe.UniverseUpdater mockUniverseUpdaterWithInactiveNodes() {
    return new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails.userIntent = new UserIntent();
        // Add a desired number of nodes.
        universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
        universeDetails.userIntent.numNodes = universeDetails.userIntent.replicationFactor;
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node = getDummyNodeDetails(idx, NodeDetails.NodeState.Running,
                                 idx <= universeDetails.userIntent.replicationFactor);
          universeDetails.nodeDetailsSet.add(node);
        }

        NodeDetails node = getDummyNodeDetails(4, NodeDetails.NodeState.BeingDecommissioned);
        universeDetails.nodeDetailsSet.add(node);
        universeDetails.nodePrefix = "host";
        universe.setUniverseDetails(universeDetails);
      }
    };
  }

  public static NodeDetails getDummyNodeDetails(int idx, NodeDetails.NodeState state) {
    return getDummyNodeDetails(idx, state, false /* isMaster */);
  }

  private static NodeDetails getDummyNodeDetails(int idx,
                                                 NodeDetails.NodeState state,
                                                 boolean isMaster) {
    NodeDetails node = new NodeDetails();
    node.nodeName = "host-n" + idx;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.cloud = "aws";
    node.cloudInfo.az = "az-" + idx;
    node.cloudInfo.region = "test-region";
    node.cloudInfo.subnet_id = "subnet-" + idx;
    node.cloudInfo.private_ip = "host-n" + idx;
    node.cloudInfo.instance_type = "c3-large";
    node.isTserver = true;
    node.state = state;
    node.isMaster = isMaster;
    node.nodeIdx = idx;
    return node;
  }

  public static TableDetails getDummyCollectionsTableDetails(ColumnDetails.YQLDataType dataType) {
    TableDetails table = getDummyTableDetails(1, 0, -1L, SortOrder.NONE);
    ColumnDetails collectionsColumn = new ColumnDetails();
    collectionsColumn.name = "v2";
    collectionsColumn.columnOrder = 2;
    collectionsColumn.type = dataType;
    collectionsColumn.keyType = ColumnDetails.YQLDataType.UUID;
    if (dataType.equals(ColumnDetails.YQLDataType.MAP)) {
      collectionsColumn.valueType = ColumnDetails.YQLDataType.VARCHAR;
    }
    table.columns.add(collectionsColumn);
    return table;
  }

  public static TableDetails getDummyTableDetailsNoClusteringKey(int partitionKeyCount, long ttl) {
    return getDummyTableDetails(partitionKeyCount, 0, ttl, SortOrder.NONE);
  }

  public static TableDetails getDummyTableDetails(int partitionKeyCount, int clusteringKeyCount,
                                                  long ttl, SortOrder sortOrder) {
    TableDetails table = new TableDetails();
    table.tableName = "dummy_table";
    table.keyspace = "dummy_ks";
    table.ttlInSeconds = ttl;
    table.columns = new LinkedList<>();
    for (int i = 0; i < partitionKeyCount + clusteringKeyCount; ++i) {
      ColumnDetails column = new ColumnDetails();
      column.name = "k" + i;
      column.columnOrder = i;
      column.type = ColumnDetails.YQLDataType.INT;
      column.isPartitionKey = i < partitionKeyCount;
      column.isClusteringKey = !column.isPartitionKey;
      if (column.isClusteringKey) {
        column.sortOrder = sortOrder;
      }
      table.columns.add(column);
    }
    ColumnDetails column = new ColumnDetails();
    column.name = "v";
    column.columnOrder = partitionKeyCount + clusteringKeyCount;
    column.type = ColumnDetails.YQLDataType.VARCHAR;
    column.isPartitionKey = false;
    column.isClusteringKey = false;
    table.columns.add(column);
    return table;
  }
}
