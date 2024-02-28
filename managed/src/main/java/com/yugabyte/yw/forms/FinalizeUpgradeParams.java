// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import java.util.Set;

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

  public static final Set<SoftwareUpgradeState> ALLOWED_FINALIZE_SOFTWARE_UPGRADE_STATE_SET =
      ImmutableSet.of(SoftwareUpgradeState.PreFinalize, SoftwareUpgradeState.FinalizeFailed);

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    if (!ALLOWED_FINALIZE_SOFTWARE_UPGRADE_STATE_SET.contains(
        universe.getUniverseDetails().softwareUpgradeState)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot finalize Software upgrade on universe which are in state "
              + universe.getUniverseDetails().softwareUpgradeState);
    }
  }

  public static class Converter extends BaseConverter<FinalizeUpgradeParams> {}
}
