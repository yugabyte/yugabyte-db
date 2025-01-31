// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.datadoghq.com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

@Slf4j
public class UpgradeWithGFlags extends UpgradeTaskParams {
  public static final String SPECIFIC_GFLAGS_NO_CHANGES_ERROR =
      "No changes in gflags (modify specificGflags in cluster)";
  public static final String EMPTY_SPECIFIC_GFLAGS =
      "Primary cluster should have non-empty specificGFlags";

  public Map<String, String> masterGFlags;
  public Map<String, String> tserverGFlags;

  @JsonIgnore GFlagsValidation gFlagsValidation;

  protected boolean verifyGFlagsHasChanges(Universe universe) {
    if (isUsingSpecificGFlags(universe)) {
      return verifySpecificGFlags(universe);
    } else {
      return verifyGFlagsOld(universe);
    }
  }

  /**
   * Verifies the preview GFlags settings for the given universe.
   *
   * @param universe The universe to verify the GFlags settings for.
   * @throws PlatformServiceException If the GFlags settings are invalid.
   */
  protected void verifyPreviewGFlagsSettings(Universe universe) {
    gFlagsValidation = StaticInjectorHolder.injector().instanceOf(GFlagsValidation.class);
    if (isUsingSpecificGFlags(universe)) {
      checkPreviewGFlagsOnSpecificGFlags(universe, gFlagsValidation);
    } else {
      checkPreviewGFlagsOnOld(universe, gFlagsValidation);
    }
  }

  /**
   * Verifies that specific gflags were changed for universe.
   *
   * @param universe
   * @return true if changed
   */
  private boolean verifySpecificGFlags(Universe universe) {
    // verify changes to groups here
    // if groups are added, cannot allow changes to those gflags
    Map<UUID, Cluster> newClusters =
        clusters.stream().collect(Collectors.toMap(c -> c.uuid, c -> c));
    boolean hasClustersToUpdate = false;
    for (Cluster curCluster : universe.getUniverseDetails().clusters) {
      Cluster newCluster = newClusters.get(curCluster.uuid);
      if (newCluster == null
          || (Objects.equals(
                  newCluster.userIntent.specificGFlags, curCluster.userIntent.specificGFlags)
              && (newCluster.userIntent.specificGFlags != null
                  && curCluster.userIntent.specificGFlags != null
                  && Objects.equals(
                      newCluster.userIntent.specificGFlags.getGflagGroups(),
                      curCluster.userIntent.specificGFlags.getGflagGroups()))
              && !skipMatchWithUserIntent)) {
        continue;
      }
      if (newCluster.clusterType == ClusterType.PRIMARY) {
        if (newCluster.userIntent.specificGFlags == null) {
          throw new PlatformServiceException(Http.Status.BAD_REQUEST, EMPTY_SPECIFIC_GFLAGS);
        }
        if (newCluster.userIntent.specificGFlags.isInheritFromPrimary()) {
          throw new PlatformServiceException(
              Http.Status.BAD_REQUEST, "Cannot inherit gflags for primary cluster");
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
                  Http.Status.BAD_REQUEST,
                  "Cannot delete gFlags through non-restart upgrade option.");
            }
          }
        }
        // check if gflag groups have changed with NON Restart. Throw error if so
        if (newCluster.userIntent.specificGFlags != null
            && curCluster.userIntent.specificGFlags != null
            && !Objects.equals(
                newCluster.userIntent.specificGFlags.getGflagGroups(),
                curCluster.userIntent.specificGFlags.getGflagGroups())) {
          throw new PlatformServiceException(
              Http.Status.BAD_REQUEST,
              "Gflag groups cannot be changed through non-restart upgrade option.");
        }
      }
    }
    return hasClustersToUpdate;
  }

  private boolean verifyGFlagsOld(Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (!skipMatchWithUserIntent
        && masterGFlags.equals(userIntent.masterGFlags)
        && tserverGFlags.equals(userIntent.tserverGFlags)) {
      return false;
    }
    boolean gFlagsDeleted =
        (!GFlagsUtil.getDeletedGFlags(userIntent.masterGFlags, masterGFlags).isEmpty())
            || (!GFlagsUtil.getDeletedGFlags(userIntent.tserverGFlags, tserverGFlags).isEmpty());
    if (gFlagsDeleted && upgradeOption.equals(UpgradeOption.NON_RESTART_UPGRADE)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Cannot delete gFlags through non-restart upgrade option.");
    }
    GFlagsUtil.checkConsistency(masterGFlags, tserverGFlags);
    return true;
  }

  /**
   * Checks the preview GFlags on specific GFlags for each cluster in the given universe.
   *
   * @param universe The universe for which the GFlags are being checked.
   * @param gFlagsValidation The GFlags validation object.
   * @throws PlatformServiceException If the GFlags are invalid.
   */
  private void checkPreviewGFlagsOnSpecificGFlags(
      Universe universe, GFlagsValidation gFlagsValidation) {
    try {
      for (Cluster cluster : clusters) {
        SpecificGFlags specificGFlags = cluster.userIntent.specificGFlags;
        if (specificGFlags == null) {
          continue;
        }
        String errMsg =
            GFlagsUtil.checkPreviewGFlagsOnSpecificGFlags(
                specificGFlags, gFlagsValidation, cluster.userIntent.ybSoftwareVersion);
        if (errMsg != null) {
          throw new PlatformServiceException(BAD_REQUEST, errMsg);
        }
      }
    } catch (IOException e) {
      log.error("Error while checking preview gflags", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * Checks the preview GFlags on an old version of the Universe.
   *
   * @param universe The Universe object.
   * @param gFlagsValidation The GFlagsValidation object.
   * @throws PlatformServiceException If the GFlags are invalid.
   */
  private void checkPreviewGFlagsOnOld(Universe universe, GFlagsValidation gFlagsValidation) {
    String ybSoftwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    try {
      for (UniverseTaskBase.ServerType serverType :
          Arrays.asList(UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)) {
        String errorMsg =
            GFlagsUtil.checkPreviewGFlags(
                serverType.equals(ServerType.MASTER) ? masterGFlags : tserverGFlags,
                ybSoftwareVersion,
                serverType,
                gFlagsValidation);
        if (errorMsg != null) {
          throw new PlatformServiceException(BAD_REQUEST, errorMsg);
        }
      }
    } catch (IOException e) {
      log.error("Error while checking preview gflags", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * Checks whether current clusters or newly provided clusters have specificGFlags
   *
   * @param universe
   * @return
   */
  protected boolean isUsingSpecificGFlags(Universe universe) {
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

  public Map<UUID, UniverseDefinitionTaskParams.Cluster> getNewVersionsOfClusters(
      Universe universe) {
    return getVersionsOfClusters(universe, this);
  }

  public Map<UUID, UniverseDefinitionTaskParams.Cluster> getVersionsOfClusters(
      Universe universe, UpgradeWithGFlags params) {
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
        newVersion.userIntent.masterGFlags = params.masterGFlags;
        newVersion.userIntent.tserverGFlags = params.tserverGFlags;
      } else if (clusterFromParams != null) {
        newVersion.userIntent.specificGFlags = clusterFromParams.userIntent.specificGFlags;
      }
      result.put(newVersion.uuid, newVersion);
    }
    return result;
  }

  public static boolean nodeHasGflagsChanges(
      NodeDetails node,
      UniverseTaskBase.ServerType serverType,
      UniverseDefinitionTaskParams.Cluster curCluster,
      Collection<Cluster> curClusters,
      UniverseDefinitionTaskParams.Cluster newClusterVersion,
      Collection<Cluster> newClusters) {
    Map<String, String> newGflags =
        GFlagsUtil.getGFlagsForNode(node, serverType, newClusterVersion, newClusters);
    Map<String, String> oldGflags =
        GFlagsUtil.getGFlagsForNode(node, serverType, curCluster, curClusters);
    boolean result = !newGflags.equals(oldGflags);
    log.debug("Node {} type {} has changes: {}", node.getNodeName(), serverType, result);
    return result;
  }
}
