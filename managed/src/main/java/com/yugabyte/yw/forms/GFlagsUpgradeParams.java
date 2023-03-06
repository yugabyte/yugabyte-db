// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = GFlagsUpgradeParams.Converter.class)
@Slf4j
public class GFlagsUpgradeParams extends UpgradeTaskParams {

  public Map<String, String> masterGFlags = new HashMap<>();
  public Map<String, String> tserverGFlags = new HashMap<>();

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);
    if (isUsingSpecificGFlags(universe)) {
      verifySpecificGFlags(universe);
    } else {
      verifyParamsOld(universe);
    }
  }

  private void verifySpecificGFlags(Universe universe) {
    Map<UUID, Cluster> newClusters =
        clusters.stream().collect(Collectors.toMap(c -> c.uuid, c -> c));
    boolean hasClustersToUpdate = false;
    for (Cluster curCluster : universe.getUniverseDetails().clusters) {
      Cluster newCluster = newClusters.get(curCluster.uuid);
      if (newCluster == null
          || Objects.equals(
              newCluster.userIntent.specificGFlags, curCluster.userIntent.specificGFlags)) {
        continue;
      }
      if (newCluster.clusterType == ClusterType.PRIMARY) {
        if (newCluster.userIntent.specificGFlags == null) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST, "Primary cluster should have non-empty specificGFlags");
        }
        if (newCluster.userIntent.specificGFlags.isInheritFromPrimary()) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST, "Cannot inherit gflags for primary cluster");
        }
      }
      if (newCluster.userIntent.specificGFlags != null) {
        newCluster.userIntent.specificGFlags.validateConsistency();
      }
      hasClustersToUpdate = true;
      if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
        for (NodeDetails nodeDetails : universe.getNodesInCluster(curCluster.uuid)) {
          for (UniverseTaskBase.ServerType serverType :
              Arrays.asList(
                  UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)) {
            Map<String, String> newGflags =
                GFlagsUtil.getGFlagsForNode(
                    nodeDetails, serverType, newCluster, newClusters.values());
            Map<String, String> currentGflags =
                GFlagsUtil.getGFlagsForNode(
                    nodeDetails, serverType, curCluster, universe.getUniverseDetails().clusters);
            if (!GFlagsUtil.getDeletedGFlags(currentGflags, newGflags).isEmpty()) {
              throw new PlatformServiceException(
                  Status.BAD_REQUEST, "Cannot delete gFlags through non-restart upgrade option.");
            }
          }
        }
      }
    }
    if (!hasClustersToUpdate) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "No changes in gflags (modify specificGflags in cluster)");
    }
  }

  private void verifyParamsOld(Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (masterGFlags.equals(userIntent.masterGFlags)
        && tserverGFlags.equals(userIntent.tserverGFlags)) {
      if (masterGFlags.isEmpty() && tserverGFlags.isEmpty()) {
        throw new PlatformServiceException(Status.BAD_REQUEST, "gflags param is required.");
      }
      throw new PlatformServiceException(Status.BAD_REQUEST, "No gflags to change.");
    }
    boolean gFlagsDeleted =
        (!GFlagsUtil.getDeletedGFlags(userIntent.masterGFlags, masterGFlags).isEmpty())
            || (!GFlagsUtil.getDeletedGFlags(userIntent.tserverGFlags, tserverGFlags).isEmpty());
    if (gFlagsDeleted && upgradeOption.equals(UpgradeOption.NON_RESTART_UPGRADE)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Cannot delete gFlags through non-restart upgrade option.");
    }
    GFlagsUtil.checkConsistency(masterGFlags, tserverGFlags);
  }

  public Map<UUID, UniverseDefinitionTaskParams.Cluster> getNewVersionsOfClusters(
      Universe universe) {
    boolean usingSpecificGFlags = isUsingSpecificGFlags(universe);
    Map<UUID, Cluster> clustersFromParams =
        clusters.stream().collect(Collectors.toMap(c -> c.uuid, c -> c));
    Map<UUID, UniverseDefinitionTaskParams.Cluster> result = new HashMap<>();
    for (UniverseDefinitionTaskParams.Cluster curCluster : universe.getUniverseDetails().clusters) {
      Cluster clusterFromParams = clustersFromParams.get(curCluster.uuid);
      UniverseDefinitionTaskParams.Cluster newVersion =
          new UniverseDefinitionTaskParams.Cluster(
              curCluster.clusterType, curCluster.userIntent.clone());
      newVersion.uuid = curCluster.uuid;
      if (!usingSpecificGFlags) {
        newVersion.userIntent.masterGFlags = this.masterGFlags;
        newVersion.userIntent.tserverGFlags = this.tserverGFlags;
      } else if (clusterFromParams != null) {
        newVersion.userIntent.specificGFlags = clusterFromParams.userIntent.specificGFlags;
      }
      result.put(newVersion.uuid, newVersion);
    }
    return result;
  }

  /**
   * Checks whether current clusters or newly provided clusters have specificGFlags
   *
   * @param universe
   * @return
   */
  private boolean isUsingSpecificGFlags(Universe universe) {
    boolean result =
        Stream.concat(universe.getUniverseDetails().clusters.stream(), this.clusters.stream())
            .filter(c -> c.userIntent.specificGFlags != null)
            .findFirst()
            .isPresent();
    if (result) {
      log.debug("Using specific gflags");
    }
    return result;
  }

  public static class Converter extends BaseConverter<GFlagsUpgradeParams> {}
}
