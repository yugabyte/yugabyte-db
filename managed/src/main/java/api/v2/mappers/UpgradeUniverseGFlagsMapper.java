// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.UpgradeUniverseGFlags;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;
import play.mvc.Http.Status;

@Mapper(uses = UpgradeOptionEnumMapper.class, config = CentralConfig.class)
public interface UpgradeUniverseGFlagsMapper {
  UpgradeUniverseGFlagsMapper INSTANCE = Mappers.getMapper(UpgradeUniverseGFlagsMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "clusters", source = "source")
  GFlagsUpgradeParams copyToV1GFlagsUpgradeParams(
      UpgradeUniverseGFlags source, @MappingTarget GFlagsUpgradeParams target);

  default List<Cluster> updateClusterGFlags(
      UpgradeUniverseGFlags source, @MappingTarget List<Cluster> targetClusters) {
    if (source.getUniverseGflags() == null) {
      return targetClusters;
    }
    source
        .getUniverseGflags()
        .forEach(
            (clusterUuid, clusterGFlags) -> {
              Cluster cluster =
                  targetClusters.stream()
                      .filter(c -> c.uuid.equals(UUID.fromString(clusterUuid)))
                      .findAny()
                      .orElse(null);
              if (cluster == null) {
                throw new PlatformServiceException(
                    Status.NOT_FOUND, "Cluster ID not found " + clusterUuid);
              }
              cluster.userIntent.specificGFlags =
                  SpecificGFlags.construct(clusterGFlags.getMaster(), clusterGFlags.getTserver());
              if (clusterGFlags.getAzGflags() != null) {
                Map<UUID, PerProcessFlags> perAZ = new HashMap<>();
                clusterGFlags
                    .getAzGflags()
                    .forEach(
                        (azuuid, azGFlags) -> {
                          PerProcessFlags perProcessFlags = new PerProcessFlags();
                          perProcessFlags.value =
                              ImmutableMap.of(
                                  ServerType.MASTER,
                                  azGFlags.getMaster(),
                                  ServerType.TSERVER,
                                  azGFlags.getTserver());
                          perAZ.put(UUID.fromString(azuuid), perProcessFlags);
                        });
                cluster.userIntent.specificGFlags.setPerAZ(perAZ);
              }
            });
    return targetClusters;
  }
}
