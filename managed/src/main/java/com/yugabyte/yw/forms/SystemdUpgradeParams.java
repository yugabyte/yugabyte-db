// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = SystemdUpgradeParams.Converter.class)
public class SystemdUpgradeParams extends UpgradeTaskParams {

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Only ROLLING_UPGRADE option is supported for systemd upgrades.");
    }

    if (Util.isOnPremManualProvisioning(universe)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Cannot upgrade systemd for manually provisioned universes");
    }
  }

  public static class Converter extends BaseConverter<SystemdUpgradeParams> {}
}
