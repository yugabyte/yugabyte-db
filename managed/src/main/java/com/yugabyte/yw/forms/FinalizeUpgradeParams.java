// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = FinalizeUpgradeParams.Converter.class)
public class FinalizeUpgradeParams extends UpgradeTaskParams {

  public boolean upgradeSystemCatalog = true;

  public FinalizeUpgradeParams() {}

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public UniverseDefinitionTaskParams.SoftwareUpgradeState
      getUniverseSoftwareUpgradeStateOnFailure() {
    return UniverseDefinitionTaskParams.SoftwareUpgradeState.FinalizeFailed;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    if (!universe
        .getUniverseDetails()
        .softwareUpgradeState
        .equals(SoftwareUpgradeState.PreFinalize)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot finalize Software upgrade on universe which are not in pre-finalize state.");
    }
  }

  public static class Converter extends BaseConverter<FinalizeUpgradeParams> {}
}
