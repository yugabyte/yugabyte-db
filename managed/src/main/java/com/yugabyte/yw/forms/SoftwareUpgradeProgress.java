// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;

/** Software upgrade progress by AZ (standard and canary), exposed on task APIs. */
@ApiModel(description = "Software upgrade progress by availability zone")
@Data
public class SoftwareUpgradeProgress {

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. True when canary upgrade"
              + " configuration is in use")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private boolean isCanaryUpgrade;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Current canary pause state, if"
              + " applicable")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private CanaryPauseState canaryPauseState = null;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Master upgrade state per availability"
              + " zone")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private List<AZUpgradeState> masterAZUpgradeStatesList = new ArrayList<>();

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Tserver upgrade state per"
              + " availability zone")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private List<AZUpgradeState> tserverAZUpgradeStatesList = new ArrayList<>();

  /** Builds API progress from persisted universe state, or null when there is nothing to show. */
  public static SoftwareUpgradeProgress fromPrevYBSoftwareConfigIfPresent(
      UniverseDefinitionTaskParams.PrevYBSoftwareConfig prev) {
    if (prev == null) {
      return null;
    }
    boolean hasProgress =
        prev.isCanaryUpgrade()
            || CollectionUtils.isNotEmpty(prev.getMasterAZUpgradeStatesList())
            || CollectionUtils.isNotEmpty(prev.getTserverAZUpgradeStatesList());
    if (!hasProgress) {
      return null;
    }
    SoftwareUpgradeProgress p = new SoftwareUpgradeProgress();
    p.setCanaryUpgrade(prev.isCanaryUpgrade());
    p.setCanaryPauseState(prev.getCanaryPauseState());
    p.setMasterAZUpgradeStatesList(
        prev.getMasterAZUpgradeStatesList() != null
            ? prev.getMasterAZUpgradeStatesList().stream()
                .map(AZUpgradeState::new)
                .collect(Collectors.toList())
            : new ArrayList<>());
    p.setTserverAZUpgradeStatesList(
        prev.getTserverAZUpgradeStatesList() != null
            ? prev.getTserverAZUpgradeStatesList().stream()
                .map(AZUpgradeState::new)
                .collect(Collectors.toList())
            : new ArrayList<>());
    return p;
  }
}
