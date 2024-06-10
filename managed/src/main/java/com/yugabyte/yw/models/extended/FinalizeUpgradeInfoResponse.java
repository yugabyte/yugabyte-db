// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.extended;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.Data;

@Data
@ApiModel(description = "Finalize Upgrade Info Response")
public class FinalizeUpgradeInfoResponse {

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. List of xCluster connected universes"
              + " details to be impacted ",
      accessMode = AccessMode.READ_ONLY)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.2.0")
  private List<ImpactedXClusterConnectedUniverse> impactedXClusterConnectedUniverse =
      new ArrayList<>();

  public static class ImpactedXClusterConnectedUniverse {
    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. Impacted Universe UUID ",
        accessMode = AccessMode.READ_ONLY)
    @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.2.0")
    public UUID universeUUID;

    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. Impacted Universe name ",
        accessMode = AccessMode.READ_ONLY)
    @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.2.0")
    public String universeName;

    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. Impacted Universe version ",
        accessMode = AccessMode.READ_ONLY)
    @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.2.0")
    public String ybSoftwareVersion;
  }
}
