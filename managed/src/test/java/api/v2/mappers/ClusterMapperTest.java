// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import api.v2.models.ClusterPartitionSpec;
import api.v2.models.ClusterPlacementSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.PlacementCloud;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PartitionInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.UUID;
import org.junit.Test;

/**
 * default_region is deprecated in favour of the default partition: it must be dropped from
 * partition placements, but kept on the cluster-level placement spec.
 */
public class ClusterMapperTest {

  private static ClusterPlacementSpec placementWithDefaultRegion(UUID cloudUuid, UUID regionUuid) {
    ClusterPlacementSpec placement = new ClusterPlacementSpec();
    PlacementCloud cloud = new PlacementCloud();
    cloud.setUuid(cloudUuid);
    cloud.setCode("aws");
    cloud.setDefaultRegion(regionUuid);
    placement.addCloudListItem(cloud);
    return placement;
  }

  @Test
  public void toV1ClusterClearsDefaultRegionInPartitionsOnly() {
    UUID cloudUuid = UUID.randomUUID();
    UUID regionUuid = UUID.randomUUID();

    ClusterPartitionSpec partition = new ClusterPartitionSpec();
    partition.setName("Default");
    partition.setDefaultPartition(true);
    partition.setReplicationFactor(3);
    partition.setPlacement(placementWithDefaultRegion(cloudUuid, regionUuid));

    ClusterSpec spec = new ClusterSpec();
    spec.setPlacementSpec(placementWithDefaultRegion(cloudUuid, regionUuid));
    spec.addPartitionsSpecItem(partition);

    Cluster cluster = ClusterMapper.INSTANCE.toV1Cluster(spec);

    // Cluster-level placement keeps it (non-partitioned universes still use it).
    assertEquals(regionUuid, cluster.placementInfo.cloudList.get(0).defaultRegion);
    // Partition placement drops it.
    PartitionInfo v1Partition = cluster.getPartitions().get(0);
    assertEquals(cloudUuid, v1Partition.getPlacement().cloudList.get(0).uuid);
    assertNull(v1Partition.getPlacement().cloudList.get(0).defaultRegion);
    assertEquals(3, v1Partition.getReplicationFactor());
  }

  @Test
  public void toV2PartitionSpecClearsDefaultRegionInPartitions() {
    UUID regionUuid = UUID.randomUUID();
    PlacementInfo.PlacementCloud cloud = new PlacementInfo.PlacementCloud();
    cloud.uuid = UUID.randomUUID();
    cloud.code = "aws";
    cloud.defaultRegion = regionUuid;
    PlacementInfo placement = new PlacementInfo();
    placement.cloudList.add(cloud);

    PartitionInfo partition = new PartitionInfo();
    partition.setName("Default");
    partition.setDefaultPartition(true);
    partition.setReplicationFactor(3);
    partition.setPlacement(placement);

    ClusterPartitionSpec v2Partition = ClusterMapper.INSTANCE.toV2PartitionSpec(partition);

    assertEquals(cloud.uuid, v2Partition.getPlacement().getCloudList().get(0).getUuid());
    assertNull(v2Partition.getPlacement().getCloudList().get(0).getDefaultRegion());
    // Source V1 object is not mutated.
    assertEquals(regionUuid, cloud.defaultRegion);
  }
}
