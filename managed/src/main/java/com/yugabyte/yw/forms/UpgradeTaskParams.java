// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Map;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = UpgradeTaskParams.Converter.class)
public class UpgradeTaskParams extends UniverseDefinitionTaskParams {
  public static final int DEFAULT_SLEEP_AFTER_RESTART_MS = 180000;

  public UpgradeOption upgradeOption = UpgradeOption.ROLLING_UPGRADE;
  public Integer sleepAfterMasterRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;
  public Integer sleepAfterTServerRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;

  public enum UpgradeTaskType {
    Everything,
    Software,
    Systemd,
    VMImage,
    GFlags,
    Restart,
    Certs,
    ToggleTls,
    ResizeNode,
    Reboot
  }

  public enum UpgradeTaskSubType {
    None,
    Download,
    Install,
    CopyCerts,
    Round1GFlagsUpdate,
    Round2GFlagsUpdate,
    PackageReInstall,
    YbcInstall,
    YbcGflagsUpdate
  }

  public enum UpgradeOption {
    @JsonProperty("Rolling")
    ROLLING_UPGRADE,
    @JsonProperty("Non-Rolling")
    NON_ROLLING_UPGRADE,
    @JsonProperty("Non-Restart")
    NON_RESTART_UPGRADE
  }

  public boolean isKubernetesUpgradeSupported() {
    return false;
  }

  public void verifyParams(Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    Map<String, String> universeConfig = universe.getConfig();

    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE && universe.nodesInTransit()) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Cannot perform a rolling upgrade on universe "
              + universe.universeUUID
              + " as it has nodes in one of "
              + NodeDetails.IN_TRANSIT_STATES
              + " states.");
    }

    if (isKubernetesUpgradeSupported() && userIntent.providerType.equals(CloudType.kubernetes)) {
      if (!universeConfig.containsKey(Universe.HELM2_LEGACY)) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Cannot perform upgrade on universe. "
                + universe.universeUUID
                + " as it is not helm 3 compatible. "
                + "Manually migrate the deployment to helm3 "
                + "and then mark the universe as helm 3 compatible.");
      }
    }

    if (!isKubernetesUpgradeSupported() && userIntent.providerType.equals(CloudType.kubernetes)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Kubernetes Upgrade is not supported.");
    }
  }

  public static class Converter extends BaseConverter<UpgradeTaskParams> {}
}
