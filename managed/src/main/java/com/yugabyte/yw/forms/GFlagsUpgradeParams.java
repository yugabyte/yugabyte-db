// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = GFlagsUpgradeParams.Converter.class)
@Slf4j
public class GFlagsUpgradeParams extends UpgradeWithGFlags {

  protected RuntimeConfGetter runtimeConfGetter;

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    runtimeConfGetter = StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);

    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)
        && !runtimeConfGetter.getConfForScope(
            universe, UniverseConfKeys.allowGFlagsOverrideDuringPreFinalize)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot upgrade gflags on universe in state "
              + universe.getUniverseDetails().softwareUpgradeState);
    }

    if (masterGFlags == null) {
      masterGFlags = new HashMap<>();
    }
    if (tserverGFlags == null) {
      tserverGFlags = new HashMap<>();
    }
    verifyGFlags(universe, isFirstTry);
  }

  public void checkXClusterAutoFlags(
      Universe universe,
      GFlagsValidation gFlagsValidation,
      XClusterUniverseService xClusterUniverseService) {
    super.verifyParams(universe, true);
    if (isUsingSpecificGFlags(universe)) {
      checkXClusterSpecificAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
    } else {
      checkXClusterOldAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
    }
  }

  private void checkXClusterSpecificAutoFlags(
      Universe universe,
      GFlagsValidation gFlagsValidation,
      XClusterUniverseService xClusterUniverseService) {
    try {
      Set<UUID> xClusterConnectedUniverseUUIDSet =
          xClusterUniverseService.getActiveXClusterSourceAndTargetUniverseSet(
              universe.getUniverseUUID());
      if (CollectionUtils.isEmpty(xClusterConnectedUniverseUUIDSet)) {
        return;
      }
      String softwareVersion =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      Map<UUID, Cluster> newClusters =
          clusters.stream().collect(Collectors.toMap(c -> c.uuid, c -> c));
      for (Cluster curCluster : universe.getUniverseDetails().clusters) {
        Cluster newCluster = newClusters.get(curCluster.uuid);
        for (NodeDetails nodeDetails : universe.getNodesInCluster(curCluster.uuid)) {
          for (UniverseTaskBase.ServerType serverType :
              Arrays.asList(
                  UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)) {
            Map<String, String> newGFlags =
                GFlagsUtil.getGFlagsForNode(
                    nodeDetails, serverType, newCluster, newClusters.values());
            Set<String> newAutoFlags =
                gFlagsValidation
                    .getFilteredAutoFlagsWithNonInitialValue(newGFlags, softwareVersion, serverType)
                    .keySet();
            for (UUID univUUID : xClusterConnectedUniverseUUIDSet) {
              if (!CollectionUtils.isEmpty(newAutoFlags)) {
                Universe xClusterUniverse = Universe.getOrBadRequest(univUUID);
                checkAutoFlagsCompatibility(
                    newAutoFlags, serverType, xClusterUniverse, gFlagsValidation);
              }
            }
          }
        }
      }
    } catch (IOException e) {
      log.error("Error occurred: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private void checkXClusterOldAutoFlags(
      Universe universe,
      GFlagsValidation gFlagsValidation,
      XClusterUniverseService xClusterUniverseService) {
    try {
      Set<UUID> xClusterConnectedUniverseUUIDSet =
          xClusterUniverseService.getActiveXClusterSourceAndTargetUniverseSet(
              universe.getUniverseUUID());
      if (CollectionUtils.isEmpty(xClusterConnectedUniverseUUIDSet)) {
        return;
      }
      String softwareVersion =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      Set<String> newMasterAutoFlags =
          gFlagsValidation
              .getFilteredAutoFlagsWithNonInitialValue(
                  masterGFlags, softwareVersion, UniverseTaskBase.ServerType.MASTER)
              .keySet();
      Set<String> newTserverAutoFlags =
          gFlagsValidation
              .getFilteredAutoFlagsWithNonInitialValue(
                  tserverGFlags, softwareVersion, UniverseTaskBase.ServerType.TSERVER)
              .keySet();
      for (UUID univUUID : xClusterConnectedUniverseUUIDSet) {
        Universe xClusterUniverse = Universe.getOrBadRequest(univUUID);
        checkAutoFlagsCompatibility(
            newMasterAutoFlags,
            UniverseTaskBase.ServerType.MASTER,
            xClusterUniverse,
            gFlagsValidation);
        checkAutoFlagsCompatibility(
            newTserverAutoFlags,
            UniverseTaskBase.ServerType.TSERVER,
            xClusterUniverse,
            gFlagsValidation);
      }
    } catch (IOException e) {
      log.error("Error occurred: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private void checkAutoFlagsCompatibility(
      Set<String> userAutoFlagsSet,
      UniverseTaskBase.ServerType serverType,
      Universe universe,
      GFlagsValidation gFlagsValidation)
      throws IOException {
    if (CollectionUtils.isEmpty(userAutoFlagsSet)) {
      return;
    }
    // Fetch set of auto flags supported on xCluster Universe.
    String xClusterUniverseSoftwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (!CommonUtils.isAutoFlagSupported(xClusterUniverseSoftwareVersion)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot upgrade auto flags as an XCluster linked universe "
              + universe.getUniverseUUID()
              + " does not support auto flags");
    }
    Set<String> xClusterUniverseAutoFlags =
        gFlagsValidation
            .extractAutoFlags(xClusterUniverseSoftwareVersion, serverType)
            .autoFlagDetails
            .stream()
            .map(autoFlagDetails -> autoFlagDetails.name)
            .collect(Collectors.toSet());
    // Check if all user overridden auto flags are supported on xCluster universe.
    userAutoFlagsSet.forEach(
        flag -> {
          if (!xClusterUniverseAutoFlags.contains(flag)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                flag
                    + " is not present in the xCluster linked universe: "
                    + universe.getUniverseUUID());
          }
        });
  }

  public static class Converter extends BaseConverter<GFlagsUpgradeParams> {}
}
