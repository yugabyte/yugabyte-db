// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = SystemdUpgradeParams.Converter.class)
public class SystemdUpgradeParams extends UpgradeTaskParams {

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    verifyParams(universe, null, isFirstTry);
  }

  @Override
  public void verifyParams(Universe universe, NodeState nodeState, boolean isFirstTry) {
    super.verifyParams(universe, nodeState, isFirstTry);

    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Only ROLLING_UPGRADE option is supported for systemd upgrades.");
    }
  }

  public static class Converter extends BaseConverter<SystemdUpgradeParams> {}
}
