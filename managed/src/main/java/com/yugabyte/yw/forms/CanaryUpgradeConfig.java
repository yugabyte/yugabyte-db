// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;

/** Configuration for canary upgrade. Preview API since 2026.1.0.0-b0. */
@ApiModel(description = "Canary upgrade configuration")
public class CanaryUpgradeConfig {

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Whether to pause after all masters"
              + " are upgraded",
      example = "true")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  public boolean pauseAfterMasters = false;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Primary cluster: ordered list of AZ"
              + " upgrade steps. List order = upgrade order. Each step can optionally pause after"
              + " tserver upgrade. Null = use default AZ order with no pause points.",
      example =
          "[{\"azUUID\": \"az-1-uuid\", \"pauseAfterTserverUpgrade\": true}, {\"azUUID\":"
              + " \"az-2-uuid\", \"pauseAfterTserverUpgrade\": false}]")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  public List<AZUpgradeStep> primaryClusterAZSteps = null;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Read replica cluster: ordered list of"
              + " AZ upgrade steps. Only applicable if universe has read replica clusters. Null ="
              + " use default AZ order with no pause points.",
      example =
          "[{\"azUUID\": \"az-4-uuid\", \"pauseAfterTserverUpgrade\": true}, {\"azUUID\":"
              + " \"az-5-uuid\", \"pauseAfterTserverUpgrade\": false}]")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  public List<AZUpgradeStep> readReplicaClusterAZSteps = null;
}
