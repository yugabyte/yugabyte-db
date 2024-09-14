// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = KubernetesOverridesUpgradeParams.Converter.class)
public class KubernetesOverridesUpgradeParams extends UpgradeTaskParams {

  public String universeOverrides = new String();
  public Map<String, String> azOverrides = new HashMap<>();

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Check if universe type is kubernetes.
    if (userIntent.providerType != CloudType.kubernetes) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "The universe type must be kubernetes to use k8s overrides upgrade API.");
    }

    // Verify service endpoints
    try {
      KubernetesUtil.validateUpgradeServiceEndpoints(
          universeOverrides, azOverrides, universe.getUniverseDetails(), universe.getConfig());
    } catch (IOException e) {
      throw new RuntimeException("Error occured while parsing overrides", e.getCause());
    }

    // Check if overrides changed.
    Map<String, String> flatUniverseOverrides =
        HelmUtils.flattenMap(HelmUtils.convertYamlToMap(universeOverrides));
    Map<String, String> flatCurUniverseOverrides =
        HelmUtils.flattenMap(HelmUtils.convertYamlToMap(userIntent.universeOverrides));
    if (!flatUniverseOverrides.equals(flatCurUniverseOverrides)) {
      return; // universe overrides modified.
    }

    // Avoid NPEs
    if (userIntent.azOverrides == null) userIntent.azOverrides = new HashMap<>();

    if (azOverrides.size() != userIntent.azOverrides.size()) {
      return; // azOverrides modified.
    }

    Set<String> extraAZs = Sets.difference(azOverrides.keySet(), userIntent.azOverrides.keySet());
    if (!extraAZs.isEmpty()) {
      return; // extra azs found.
    }

    for (String az : azOverrides.keySet()) { // Check each AZ keys.
      String newAZOverridesStr = azOverrides.get(az);
      Map<String, Object> newAZOverrides = HelmUtils.convertYamlToMap(newAZOverridesStr);
      String curAZOverridesStr = userIntent.azOverrides.get(az);
      Map<String, Object> curAZOverrides = HelmUtils.convertYamlToMap(curAZOverridesStr);
      if (!curAZOverrides.equals(newAZOverrides)) {
        return; // az content changed.
      }
    }

    // Sync intent and helm override values despite no changes.
    if (skipMatchWithUserIntent) {
      return;
    }

    // If we reach this point, none of the overrides changed. RFC: If upgrade fails, next upgrade
    // might fail if user applies same overrides.
    // This is the case with any edit/upgrade functionality in k8s universes.
    throw new PlatformServiceException(Status.BAD_REQUEST, "No kubernetes overrides modified.");
  }

  public static class Converter extends BaseConverter<KubernetesOverridesUpgradeParams> {}
}
