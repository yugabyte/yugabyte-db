// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.models.Universe;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = RollbackUpgradeParams.Converter.class)
public class RollbackUpgradeParams extends UpgradeTaskParams {

  public RollbackUpgradeParams() {}

  @Override
  public boolean isKubernetesUpgradeSupported() {
    // We will support it for k8s just after we are done with VM based
    // universe upgrade.
    return false;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
  }

  public static class Converter extends BaseConverter<RollbackUpgradeParams> {}
}
