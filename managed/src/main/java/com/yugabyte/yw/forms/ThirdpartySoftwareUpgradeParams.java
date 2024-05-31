// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ThirdpartySoftwareUpgradeParams.Converter.class)
@Data
@EqualsAndHashCode(callSuper = true)
@Slf4j
public class ThirdpartySoftwareUpgradeParams extends UpgradeTaskParams {

  private boolean forceAll;

  public ThirdpartySoftwareUpgradeParams() {}

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "Only ROLLING_UPGRADE option is supported for upgrade thirdparty software.");
    }
    if (Util.isOnPremManualProvisioning(universe)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "Cannot run thirdparty software upgrade for onprem with manual provisioning");
    }
  }

  public static class Converter extends BaseConverter<ThirdpartySoftwareUpgradeParams> {}
}
