// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.extended;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.Set;
import java.util.UUID;
import lombok.Data;

@Data
@ApiModel(description = "Finalize Upgrade Info Response")
public class FinalizeUpgradeInfoResponse {

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Set of xCluster connected universes"
              + " uuids to be impacted ",
      accessMode = AccessMode.READ_ONLY)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.2.0")
  private Set<UUID> impactedXClusterConnectedUniverse;
}
