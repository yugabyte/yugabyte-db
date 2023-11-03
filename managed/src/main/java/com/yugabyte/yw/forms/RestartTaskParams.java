// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = RestartTaskParams.Converter.class)
public class RestartTaskParams extends UpgradeTaskParams {
  public RestartTaskParams() {}

  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    // Only a "Rolling Upgrade" type of restart is allowed on a K8S universe
    CloudType currCloudType =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE
        && currCloudType.equals(CloudType.kubernetes)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Can perform only a rolling upgrade on a Kubernetes universe "
              + universe.getUniverseUUID());
    }
  }

  public static class Converter extends BaseConverter<RestartTaskParams> {}
}
