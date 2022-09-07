// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import java.util.Map;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = KubernetesOverridesUpgradeParams.Converter.class)
public class KubernetesOverridesUpgradeParams extends UpgradeTaskParams {

  public String universeOverrides = new String();
  public String azOverrides = new String();

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Check if universe type is kubernetes.
    if (userIntent.providerType != CloudType.kubernetes) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "The universe type must be kubernetes to use k8s overrides upgrade API.");
    }

    // Check if overrides changed.
    Map<String, String> flatUniverseOverrides =
        HelmUtils.flattenMap(HelmUtils.convertYamlToMap(universeOverrides));
    Map<String, String> flatAZOverrides =
        HelmUtils.flattenMap(HelmUtils.convertYamlToMap(azOverrides));
    Map<String, String> flatCurUniverseOverrides =
        HelmUtils.flattenMap(HelmUtils.convertYamlToMap(userIntent.universeOverrides));
    Map<String, String> flatCurAZOverrides =
        HelmUtils.flattenMap(HelmUtils.convertYamlToMap(userIntent.azOverrides));
    if (flatUniverseOverrides.equals(flatCurUniverseOverrides)
        && flatAZOverrides.equals(flatCurAZOverrides)) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "No kubernetes overrides modified.");
    }
  }

  public static class Converter extends BaseConverter<KubernetesOverridesUpgradeParams> {}
}
