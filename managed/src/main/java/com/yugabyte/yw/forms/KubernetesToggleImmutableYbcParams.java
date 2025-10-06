// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.ApiModelProperty;
import lombok.Getter;
import lombok.Setter;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = KubernetesToggleImmutableYbcParams.Converter.class)
public class KubernetesToggleImmutableYbcParams extends UpgradeTaskParams {

  @ApiModelProperty(value = "Use YBDB image inbuilt YBC", required = true)
  @Getter
  @Setter
  private boolean useYbdbInbuiltYbc = false;

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Check if universe type is kubernetes.
    if (userIntent.providerType != CloudType.kubernetes) {
      throw new PlatformServiceException(
          BAD_REQUEST, "The universe type must be kubernetes to use ToggleImmutableYbc API.");
    }

    if (userIntent.isUseYbdbInbuiltYbc() == this.isUseYbdbInbuiltYbc()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Universe %s already has useYbdbInbuiltYbc set to %s",
              universe.getUniverseUUID(), this.isUseYbdbInbuiltYbc()));
    }
    if (this.isUseYbdbInbuiltYbc()
        && !KubernetesUtil.isUseYbdbInbuiltYbcSupported(userIntent.ybSoftwareVersion)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Universe %s does not support ybdb inbuilt ybc", universe.getUniverseUUID()));
    }
    if (this.upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("This task does not support Non-Restart upgrades"));
    }
  }

  public static class Converter extends BaseConverter<KubernetesToggleImmutableYbcParams> {}
}
