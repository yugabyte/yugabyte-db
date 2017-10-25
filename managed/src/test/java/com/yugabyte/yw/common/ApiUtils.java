// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TableDetails;
import org.yb.ColumnSchema.SortOrder;

public class ApiUtils {
  public static Universe.UniverseUpdater mockUniverseUpdater() {
    return mockUniverseUpdater("host", null);
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(Common.CloudType cloudType) {
    return mockUniverseUpdater("host", cloudType);
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(String nodePrefix) {
    return mockUniverseUpdater(nodePrefix, null);
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(final String nodePrefix,
                                                             final Common.CloudType cloudType) {
    return new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails.cloud = cloudType;
        universeDetails.userIntent = new UserIntent();
        universeDetails.userIntent.providerType = cloudType;
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
    return mockUniverseUpdater(userIntent, "host", false /* setMasters */);
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(UserIntent userIntent,
                                                             boolean setMasters) {
    return mockUniverseUpdater(userIntent, "host", setMasters);
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(UserIntent userIntent,
                                                             String nodePrefix) {
    return mockUniverseUpdater(userIntent, nodePrefix, false /* setMasters */);
  }

  public static Universe.UniverseUpdater mockUniverseUpdater(final UserIntent userIntent,
                                                             final String nodePrefix,
                                                             final boolean setMasters) {
    return new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails = new UniverseDefinitionTaskParams();
        universeDetails.userIntent = userIntent;
        universeDetails.placementInfo = PlacementInfoUtil.getPlacementInfo(userIntent);
        universeDetails.nodeDetailsSet = new HashSet<>();
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node =
              getDummyNodeDetails(idx, NodeDetails.NodeState.Running,
                  setMasters && idx <= universeDetails.userIntent.replicationFactor);
          universeDetails.nodeDetailsSet.add(node);
        }
        universeDetails.nodePrefix = nodePrefix;
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
          NodeDetails node = getDummyNodeDetails(idx, NodeDetails.NodeState.Running);
          universeDetails.nodeDetailsSet.add(node);
        }

        NodeDetails node = getDummyNodeDetails(4, NodeDetails.NodeState.BeingDecommissioned);
        universeDetails.nodeDetailsSet.add(node);
        universeDetails.nodePrefix = "host";
        universe.setUniverseDetails(universeDetails);
      }
    };
  }

  public static UserIntent getDefaultUserIntent(Customer customer) {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i = InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UserIntent ui = getTestUserIntent(r, p, i, 3);
    ui.replicationFactor = 3;
    ui.masterGFlags = new HashMap<>();
    ui.tserverGFlags = new HashMap<>();
    return ui;
  }

  public static UserIntent getTestUserIntent(Region r, Provider p, InstanceType i, int numNodes) {
    UserIntent ui = new UserIntent();
    ui.regionList = ImmutableList.of(r.uuid);
    ui.provider = p.uuid.toString();
    ui.numNodes = numNodes;
    ui.instanceType = i.getInstanceTypeCode();
    ui.isMultiAZ = true;
    return ui;
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


  public static DeviceInfo getDummyDeviceInfo(int numVolumes, int volumeSize) {
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.numVolumes = numVolumes;
    deviceInfo.volumeSize = volumeSize;
    return deviceInfo;
  }

  public static UserIntent getDummyUserIntent(DeviceInfo deviceInfo, Provider provider,
                                              String instanceType, double spotPrice) {
    UserIntent userIntent = new UserIntent();
    userIntent.provider = provider.uuid.toString();
    userIntent.providerType = Common.CloudType.valueOf(provider.code);
    userIntent.instanceType = instanceType;
    userIntent.deviceInfo = deviceInfo;
    userIntent.spotPrice = spotPrice;
    return userIntent;
  }
}
