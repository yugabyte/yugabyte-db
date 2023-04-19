// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

@Singleton
public class XClusterUniverseService {

  private final GFlagsValidation gFlagsValidation;

  @Inject
  public XClusterUniverseService(GFlagsValidation gFlagsValidation) {
    this.gFlagsValidation = gFlagsValidation;
  }

  /**
   * Get the set of universes UUID which are connected to the input universe either as source or
   * target universe through a non-deleted universe xCluster config.
   *
   * @param universeUUID the universe on which search needs to be performed.
   * @return the set of universe uuid which are connected to the input universe.
   */
  public Set<UUID> getXClusterSourceAndTargetUniverseSet(UUID universeUUID) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByUniverseUuid(universeUUID)
            .stream()
            .filter(
                xClusterConfig ->
                    !xClusterConfig
                        .getStatus()
                        .equals(XClusterConfig.XClusterConfigStatusType.DeletedUniverse))
            .collect(Collectors.toList());
    return xClusterConfigs
        .stream()
        .map(
            config -> {
              if (config.getSourceUniverseUUID().equals(universeUUID)) {
                return config.getTargetUniverseUUID();
              } else {
                return config.getSourceUniverseUUID();
              }
            })
        .collect(Collectors.toSet());
  }
}
