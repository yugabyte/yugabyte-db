// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.Set;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = RollbackUpgradeParams.Converter.class)
public class RollbackUpgradeParams extends UpgradeTaskParams {

  // Added this field only to track the version change in the task details
  // so that customer can identify version change on older rollback tasks.
  // YBA stores this from UniverseDefinitionTaskParams.prevYBSoftwareConfig
  // runtime while initializing the task.
  @ApiModelProperty(
      value =
          "YbaApi Internal. Target software version during rollback which will be set by YBA itself"
              + " and overridden in case user provides it.",
      accessMode = AccessMode.READ_ONLY)
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.21.0.0")
  public String ybSoftwareVersion = null;

  public RollbackUpgradeParams() {}

  private static final Set<SoftwareUpgradeState> ALLOWED_UNIVERSE_SOFTWARE_UPGRADE_STATE_SET =
      ImmutableSet.of(
          SoftwareUpgradeState.PreFinalize,
          SoftwareUpgradeState.Ready,
          SoftwareUpgradeState.UpgradeFailed);

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public UniverseDefinitionTaskParams.SoftwareUpgradeState
      getUniverseSoftwareUpgradeStateOnFailure() {
    return UniverseDefinitionTaskParams.SoftwareUpgradeState.RollbackFailed;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (!ALLOWED_UNIVERSE_SOFTWARE_UPGRADE_STATE_SET.contains(
        universeDetails.softwareUpgradeState)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot rollback software upgrade on the universe in state "
              + universe.getUniverseDetails().softwareUpgradeState);
    }
    if (!universeDetails.isSoftwareRollbackAllowed
        || universeDetails.prevYBSoftwareConfig == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot rollback software upgrade as previous upgrade was finalized");
    }
  }

  public static class Converter extends BaseConverter<RollbackUpgradeParams> {}
}
