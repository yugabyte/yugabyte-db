// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.*;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = SoftwareUpgradeParams.Converter.class)
@Slf4j
public class SoftwareUpgradeParams extends UpgradeTaskParams {

  public String ybSoftwareVersion = null;
  public boolean upgradeSystemCatalog = true;

  @ApiModelProperty(
      value = "YbaApi Internal. Enable rollback support after software upgrade",
      hidden = true)
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.2.0")
  public boolean rollbackSupport = true;

  public SoftwareUpgradeParams() {}

  @JsonCreator
  public SoftwareUpgradeParams(
      @JsonProperty(value = "ybSoftwareVersion", required = false) String ybSoftwareVersion) {
    this.ybSoftwareVersion = ybSoftwareVersion;
  }

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public SoftwareUpgradeState getUniverseSoftwareUpgradeStateOnFailure() {
    return SoftwareUpgradeState.UpgradeFailed;
  }

  public static final Set<SoftwareUpgradeState> ALLOWED_UNIVERSE_SOFTWARE_UPGRADE_STATE_SET =
      ImmutableSet.of(SoftwareUpgradeState.Ready, SoftwareUpgradeState.UpgradeFailed);

  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Software upgrade cannot be non restart.");
    }

    if (upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE
        && universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.kubernetes)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Software upgrade cannot be non-rolling upgrade on Kubernetes.");
    }

    if (ybSoftwareVersion == null || ybSoftwareVersion.isEmpty()) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Invalid Yugabyte software version: " + ybSoftwareVersion);
    }

    UserIntent currentIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (isFirstTry && ybSoftwareVersion.equals(currentIntent.ybSoftwareVersion)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Software version is already: " + ybSoftwareVersion);
    }

    if (!ALLOWED_UNIVERSE_SOFTWARE_UPGRADE_STATE_SET.contains(
        universe.getUniverseDetails().softwareUpgradeState)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Software upgrade cannot be preformed on universe in state "
              + universe.getUniverseDetails().softwareUpgradeState);
    }

    RuntimeConfigFactory runtimeConfigFactory =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfigFactory.class);

    // Defaults to false, but we need to extract the variable in case the user wishes to perform a
    // downgrade with a runtime configuration override. We perform this check before verifying the
    // general SoftwareUpgradeParams to avoid introducing an API parameter.
    boolean isUniverseDowngradeAllowed =
        runtimeConfigFactory.forUniverse(universe).getBoolean("yb.upgrade.allow_downgrades");

    boolean skipVersionChecks =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getBoolean(GlobalConfKeys.skipVersionChecks.getKey());

    String currentVersion = currentIntent.ybSoftwareVersion;

    // Skip version checks if runtime flag enabled. User must take care of downgrades and cross
    // track upgrades between stable and preview versions.
    if (!skipVersionChecks) {
      boolean isCurrentVersionStable = Util.isStableVersion(currentVersion, false);
      boolean isYbSoftwareVersionStable = Util.isStableVersion(ybSoftwareVersion, false);

      // Case when trying to upgrade from stable to preview or vice versa.
      if (isCurrentVersionStable ^ isYbSoftwareVersionStable) {
        String msg =
            String.format(
                "Cannot upgrade from preview to stable version or stable to preview. If required,"
                    + " set runtime flag 'yb.skip_version_checks' to true. Tried to upgrade from"
                    + " '%s' to '%s'.",
                currentVersion, ybSoftwareVersion);
        throw new PlatformServiceException(Status.BAD_REQUEST, msg);
      }

      // Case when "yb.skip_version_checks" is false and "yb.upgrade.allow_downgrades" is false.
      // This means that downgrades are not allowed and we need to check the versions.
      if (currentVersion != null
          && !isUniverseDowngradeAllowed
          && Util.compareYbVersions(currentVersion, ybSoftwareVersion, true) > 0) {
        String msg =
            String.format(
                "DB version downgrades are not recommended, %s would downgrade from %s. Aborting "
                    + "task. To override this check and force a downgrade, please set the runtime "
                    + "config yb.upgrade.allow_downgrades to true "
                    + "(using the script set-runtime-config.sh if necessary).",
                ybSoftwareVersion, currentVersion);
        throw new PlatformServiceException(Status.BAD_REQUEST, msg);
      }
    }
  }

  public static class Converter extends BaseConverter<SoftwareUpgradeParams> {}
}
