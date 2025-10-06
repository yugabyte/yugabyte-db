// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.forms.ybc.YbcGflags;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = YbcGflagsTaskParams.Converter.class)
public class YbcGflagsTaskParams extends UpgradeTaskParams {

  @Constraints.Required
  @ApiModelProperty(value = "Customer UUID")
  public UUID customerUUID;

  @Constraints.Required
  @ApiModelProperty(value = "Universe UUID", required = true)
  @Getter
  @Setter
  private UUID universeUUID;

  @ApiModelProperty public YbcGflags ybcGflags;

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    if (!universe.getUniverseDetails().isYbcInstalled()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Ybc is not installed on the universe: " + universe.getUniverseUUID());
    }
    try {
      YbcManager.convertYbcFlagsToMap(this.ybcGflags);
    } catch (IllegalAccessException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()
        && this.upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "This task does not support Non-Restart upgrades for immutable ybc enabled"
                  + " kubernetes universes"));
    }
  }

  public static class Converter extends BaseConverter<YbcGflagsTaskParams> {}
}
