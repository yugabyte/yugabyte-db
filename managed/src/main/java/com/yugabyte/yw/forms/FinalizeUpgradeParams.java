// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.models.Universe;

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
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
  }

  public static class Converter extends BaseConverter<FinalizeUpgradeParams> {}
}
