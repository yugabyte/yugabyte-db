// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.Universe;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = SoftwareUpgradeParams.Converter.class)
public class SoftwareUpgradeParams extends UpgradeTaskParams {

  public String ybSoftwareVersion = null;
  public boolean upgradeSystemCatalog = true;

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

  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe);

    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Software upgrade cannot be non restart.");
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
    RuntimeConfigFactory runtimeConfigFactory =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfigFactory.class);

    // Defaults to false, but we need to extract the variable in case the user wishes to perform a
    // downgrade with a runtime configuration override. We perform this check before verifying the
    // general SoftwareUpgradeParams to avoid introducing an API parameter.
    boolean isUniverseDowngradeAllowed =
        runtimeConfigFactory.forUniverse(universe).getBoolean("yb.upgrade.allow_downgrades");

    String currentVersion = currentIntent.ybSoftwareVersion;

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

  public static class Converter extends BaseConverter<SoftwareUpgradeParams> {}
}
