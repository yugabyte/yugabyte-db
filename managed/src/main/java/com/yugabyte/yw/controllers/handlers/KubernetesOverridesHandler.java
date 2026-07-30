// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.forms.KubernetesOverridesResponse;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class KubernetesOverridesHandler {
  private KubernetesManagerFactory kubernetesManagerFactory;

  // RFC: In the following method we are not running 'helm template' for every AZ in overrides so if
  // there is bad config in overrides in one of the az configs and if that az is not in placement
  // during the universe creation, we may not report errors but later during edit universe user can
  // add that az into placement which has bad config in overrides, then we might hit the errors edit
  // tasks.

  @Inject
  public KubernetesOverridesHandler(KubernetesManagerFactory kubernetesManagerFactory) {
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  // Returns errors in overrides and helm template response if it fails.
  public KubernetesOverridesResponse validateKubernetesOverrides(
      UniverseConfigureTaskParams taskParams) {
    Set<String> overrideErrorsSet = new HashSet<>();
    try {
      // Check if read cluster has any overrides specified.
      if (taskParams.getReadOnlyClusters().size() != 0) {
        UniverseDefinitionTaskParams.UserIntent readClusterIntent =
            taskParams.getReadOnlyClusters().get(0).userIntent;
        if (StringUtils.isNotBlank(readClusterIntent.universeOverrides)
            || readClusterIntent.azOverrides != null && readClusterIntent.azOverrides.size() != 0) {
          overrideErrorsSet.add("Read only cluster is not allowed to have overrides");
        }
      }

      UniverseDefinitionTaskParams.UserIntent userIntent =
          taskParams.getPrimaryCluster().userIntent;
      Map<String, String> azsOverrides = userIntent.azOverrides;
      if (azsOverrides == null) {
        azsOverrides = new HashMap<>();
      }

      // For every AZ, run helm template with overrides and collect errors.
      for (UniverseDefinitionTaskParams.Cluster cluster : taskParams.clusters) {
        PlacementInfo pi = cluster.getOverallPlacement();
        overrideErrorsSet.addAll(
            validateKubernetesOverrides(
                cluster.userIntent.ybSoftwareVersion,
                taskParams.nodePrefix,
                pi,
                cluster.clusterType == UniverseDefinitionTaskParams.ClusterType.ASYNC,
                userIntent.universeOverrides,
                azsOverrides));
      }
      return KubernetesOverridesResponse.convertErrorsToKubernetesOverridesResponse(
          overrideErrorsSet);
    } catch (Exception e) {
      log.error("Exception in validating kubernetes overrides: ", e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  public Set<String> validateKubernetesOverrides(
      String ybSoftwareVersion,
      String nodePrefix,
      PlacementInfo placementInfo,
      boolean isReadonlyCluster,
      String universeOverridesStr,
      Map<String, String> azsOverrides) {
    if (azsOverrides == null) {
      azsOverrides = new HashMap<>();
    }
    Set<String> result = new HashSet<>();
    Map<String, Object> universeOverrides = new HashMap<>();
    try {
      universeOverrides = HelmUtils.convertYamlToMap(universeOverridesStr);
    } catch (Exception e) {
      log.error("Error in convertYamlToMap: ", e);
      result.add("Error: Unable to parse overrides structure, incorrect format specified");
    }
    if (CollectionUtils.isEmpty(placementInfo.cloudList)) {
      throw new PlatformServiceException(BAD_REQUEST, "Empty placement");
    }
    UUID providerUUID = placementInfo.cloudList.get(0).uuid;
    Provider provider = Provider.getOrBadRequest(providerUUID);
    Set<String> providersAZSet = new HashSet<>();
    for (Region r : provider.getRegions()) {
      for (AvailabilityZone az : r.getZones()) {
        providersAZSet.add(az.getCode());
      }
    }

    Map<UUID, Map<String, String>> configsPerAZ = KubernetesUtil.getConfigPerAZ(placementInfo);
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    Set<String> placementAZSet = new HashSet<>();

    for (Map.Entry<UUID, Map<String, String>> entry : configsPerAZ.entrySet()) {
      UUID azUUID = entry.getKey();
      String azName = AvailabilityZone.getOrBadRequest(azUUID).getCode();
      String azCode = isMultiAz ? azName : null;
      placementAZSet.add(azName);

      Map<String, String> config = entry.getValue();
      Map<String, Object> azOverrides = new HashMap<>();
      // az_overrides keys are AZ codes (same as unknown/extra AZ validation below).
      String azOverridesStr = azsOverrides.get(azName);
      if (azOverridesStr != null) {
        try {
          azOverrides = HelmUtils.convertYamlToMap(azOverridesStr);
        } catch (Exception e) {
          String errMsg =
              String.format("Error in parsing %s overrides: %s", azName, e.getMessage());
          log.error("Error in convertYamlToMap ", e);
          result.add(errMsg);
        }
      }
      String namespace =
          KubernetesUtil.getKubernetesNamespace(
              nodePrefix, azCode, config, true, /* newNamingStyle */ isReadonlyCluster);
      Set<String> partialErrorsSet =
          kubernetesManagerFactory
              .getManager()
              .validateOverrides(
                  ybSoftwareVersion, config, namespace, universeOverrides, azOverrides, azName);
      result.addAll(partialErrorsSet);
    }

    // Check user provided az which is not in provider(s).
    Set<String> unknownAZs = new TreeSet<>(Sets.difference(azsOverrides.keySet(), providersAZSet));
    if (!unknownAZs.isEmpty()) {
      result.add(
          String.format(
              "Provider %s doesn't have following AZs: %s. But they are referred in AZ overrides",
              provider.getName(), unknownAZs));
    }
    // Check for AZs which are not in placement(s).
    Set<String> extraAZs = new TreeSet<>(Sets.difference(azsOverrides.keySet(), placementAZSet));
    extraAZs.removeAll(unknownAZs);
    if (!extraAZs.isEmpty()) {
      result.add(
          String.format(
              "AZ overrides have following AZs for %s cluster: %s."
                  + " But no DB pods assigned to these AZs."
                  + "These overrides are not validated.",
              isReadonlyCluster ? "readonly" : "primary", extraAZs));
    }

    return result;
  }
}
