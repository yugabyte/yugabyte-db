// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;
import play.mvc.Http;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ThirdpartySoftwareUpgradeParams.Converter.class)
@Data
@EqualsAndHashCode(callSuper = true)
public class ThirdpartySoftwareUpgradeParams extends UpgradeTaskParams {

  private boolean forceAll;

  public ThirdpartySoftwareUpgradeParams() {}

  @Override
  public void verifyParams(Universe universe) {
    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST,
          "Only ROLLING_UPGRADE option is supported for upgrade thirdparty software.");
    }
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (userIntent.providerType == Common.CloudType.onprem) {
      boolean manualProvisioning = false;
      try {
        AccessKey accessKey =
            AccessKey.getOrBadRequest(
                UUID.fromString(userIntent.provider), userIntent.accessKeyCode);
        AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
        manualProvisioning = keyInfo.skipProvisioning;
      } catch (PlatformServiceException ex) {
        // no access code
      }
      if (manualProvisioning) {
        throw new PlatformServiceException(
            Http.Status.BAD_REQUEST,
            "Cannot run thirdparty software upgrade for onprem with manual provisioning");
      }
    }
  }

  public static class Converter extends BaseConverter<ThirdpartySoftwareUpgradeParams> {}
}
