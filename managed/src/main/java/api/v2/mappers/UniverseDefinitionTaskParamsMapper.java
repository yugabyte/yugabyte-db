// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.control.DeepClone;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class, mappingControl = DeepClone.class)
public interface UniverseDefinitionTaskParamsMapper {
  public static UniverseDefinitionTaskParamsMapper INSTANCE =
      Mappers.getMapper(UniverseDefinitionTaskParamsMapper.class);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public GFlagsUpgradeParams toGFlagsUpgradeParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public KubernetesGFlagsUpgradeParams toKubernetesGFlagsUpgradeParams(
      UniverseDefinitionTaskParams source);
}
