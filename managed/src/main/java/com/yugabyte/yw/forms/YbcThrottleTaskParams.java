// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = YbcThrottleTaskParams.Converter.class)
public class YbcThrottleTaskParams extends UpgradeTaskParams {

  @Getter @Setter private UUID customerUUID;

  @Getter @Setter private UUID universeUUID;

  @Getter @Setter private YbcThrottleParameters ybcThrottleParameters;

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
  }

  public static class Converter extends BaseConverter<YbcThrottleTaskParams> {}
}
