// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.common.Util;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = SoftwareUpgradeParams.Converter.class)
public class SoftwareUpgradeParams extends UpgradeTaskParams {

  public String ybSoftwareVersion = null;
  public boolean upgradeSystemCatalog = true;

  public SoftwareUpgradeParams() {}

  @JsonCreator
  public SoftwareUpgradeParams(
      @JsonProperty(value = "ybSoftwareVersion", required = true) String ybSoftwareVersion) {
    this.ybSoftwareVersion = ybSoftwareVersion;
  }

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);

    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Software upgrade cannot be non restart.");
    }

    if (ybSoftwareVersion == null || ybSoftwareVersion.isEmpty()) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Invalid Yugabyte software version: " + ybSoftwareVersion);
    }

    if (ybSoftwareVersion.equals(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Software version is already: " + ybSoftwareVersion);
    }
  }

  public static class Converter extends BaseConverter<SoftwareUpgradeParams> {}
}
