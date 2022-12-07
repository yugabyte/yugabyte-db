// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.Map;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = GFlagsUpgradeParams.Converter.class)
public class GFlagsUpgradeParams extends UpgradeTaskParams {

  public Map<String, String> masterGFlags = new HashMap<>();
  public Map<String, String> tserverGFlags = new HashMap<>();

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (masterGFlags.equals(userIntent.masterGFlags)
        && tserverGFlags.equals(userIntent.tserverGFlags)) {
      if (masterGFlags.isEmpty() && tserverGFlags.isEmpty()) {
        throw new PlatformServiceException(Status.BAD_REQUEST, "gflags param is required.");
      }
      throw new PlatformServiceException(Status.BAD_REQUEST, "No gflags to change.");
    }
    boolean gFlagsDeleted =
        (!GFlagsUtil.getDeletedGFlags(userIntent.masterGFlags, masterGFlags).isEmpty())
            || (!GFlagsUtil.getDeletedGFlags(userIntent.tserverGFlags, tserverGFlags).isEmpty());
    if (gFlagsDeleted && upgradeOption.equals(UpgradeOption.NON_RESTART_UPGRADE)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Cannot delete gFlags through non-restart upgrade option.");
    }
    GFlagsUtil.checkConsistency(masterGFlags, tserverGFlags);
  }

  public static class Converter extends BaseConverter<GFlagsUpgradeParams> {}
}
