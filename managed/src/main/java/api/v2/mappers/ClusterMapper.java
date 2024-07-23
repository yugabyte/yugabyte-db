// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.ClusterAddSpec;
import api.v2.models.ClusterEditSpec;
import api.v2.models.ClusterInfo;
import api.v2.models.ClusterSpec;
import api.v2.models.PlacementAZ;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.List;
import org.mapstruct.BeanMapping;
import org.mapstruct.InheritInverseConfiguration;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.NullValueCheckStrategy;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {UserIntentMapper.class})
public interface ClusterMapper {
  public static ClusterMapper INSTANCE = Mappers.getMapper(ClusterMapper.class);

  @Mapping(target = ".", source = "userIntent")
  @Mapping(target = "nodeSpec", source = "userIntent")
  @Mapping(target = "networkingSpec", source = "userIntent")
  @Mapping(target = "providerSpec", source = "userIntent")
  @Mapping(target = "placementSpec", source = "placementInfo")
  @Mapping(target = "useSpotInstance", source = "userIntent.useSpotInstance")
  @Mapping(target = "gflags", source = "userIntent")
  ClusterSpec toV2ClusterSpec(Cluster v1Cluster);

  @Mapping(target = "userIntent", source = ".")
  @Mapping(target = "placementInfo", source = "placementSpec")
  // null check is required here to avoid overwriting the auto generated uuid with null
  @Mapping(target = "uuid", nullValueCheckStrategy = NullValueCheckStrategy.ALWAYS)
  Cluster toV1Cluster(ClusterSpec clusterSpec);

  @Mapping(target = ".", source = "userIntent")
  ClusterInfo toV2ClusterInfo(Cluster v1Cluster);

  @Mapping(target = "userIntent", source = ".")
  @Mapping(target = "placementInfo", source = "placementSpec")
  Cluster toV1ClusterFromClusterEditSpec(
      ClusterEditSpec clusterEditSpec, @MappingTarget Cluster v1Cluster);

  default List<Cluster> toV1ClusterFromClusterEditSpecList(
      List<ClusterEditSpec> clusterEditSpecList, @MappingTarget List<Cluster> v1Clusters) {
    if (clusterEditSpecList == null) {
      return v1Clusters;
    }
    for (ClusterEditSpec clusterEditSpec : clusterEditSpecList) {
      Cluster v1Cluster =
          v1Clusters.stream()
              .filter(c -> c.uuid.equals(clusterEditSpec.getUuid()))
              .findAny()
              .orElseThrow();
      toV1ClusterFromClusterEditSpec(clusterEditSpec, v1Cluster);
    }
    return v1Clusters;
  }

  @BeanMapping(nullValueCheckStrategy = NullValueCheckStrategy.ALWAYS)
  @Mapping(target = "placementSpec", ignore = true)
  ClusterSpec deepCopyClusterSpecWithoutPlacementSpec(
      ClusterSpec source, @MappingTarget ClusterSpec target);

  @BeanMapping(nullValueCheckStrategy = NullValueCheckStrategy.ALWAYS)
  ClusterSpec deepCopyClusterSpec(ClusterSpec source, @MappingTarget ClusterSpec target);

  @BeanMapping(nullValueCheckStrategy = NullValueCheckStrategy.ALWAYS)
  @Mapping(target = "placementSpec", ignore = true)
  ClusterEditSpec deepCopyClusterEditSpecWithoutPlacementSpec(
      ClusterSpec source, @MappingTarget ClusterEditSpec target);

  @BeanMapping(nullValueCheckStrategy = NullValueCheckStrategy.ALWAYS)
  ClusterEditSpec deepCopyClusterEditSpec(
      ClusterEditSpec source, @MappingTarget ClusterEditSpec target);

  @Mapping(target = "userIntent", source = ".")
  @Mapping(target = "placementInfo", source = "placementSpec")
  Cluster overwriteClusterAddSpec(
      ClusterAddSpec clusterAddSpec, @MappingTarget Cluster newReadReplica);

  // used implicitly in above mapping
  @Mapping(target = "numNodesInAZ", source = "numNodesInAz")
  @Mapping(target = "isAffinitized", source = "leaderAffinity")
  PlacementInfo.PlacementAZ toV1PlacementAZ(PlacementAZ placementAZ);

  @InheritInverseConfiguration
  PlacementAZ toV2PlacementAZ(PlacementInfo.PlacementAZ placementAZ);

  @ValueMappings(@ValueMapping(target = "ASYNC", source = "READ_REPLICA"))
  UniverseDefinitionTaskParams.ClusterType toV1ClusterType(
      ClusterAddSpec.ClusterTypeEnum v2ClusterType);
}
