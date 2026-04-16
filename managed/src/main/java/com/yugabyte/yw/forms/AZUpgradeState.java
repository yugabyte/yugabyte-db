// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

/** One AZ's upgrade status for a cluster and server role. */
@ApiModel(description = "Availability zone software upgrade state")
@Data
@NoArgsConstructor
@AllArgsConstructor
public class AZUpgradeState {

  public AZUpgradeState(AZUpgradeState other) {
    this(other.azUUID, other.azName, other.serverType, other.clusterUUID, other.status);
  }

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Availability zone UUID for this"
              + " upgrade state entry")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private UUID azUUID;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Availability zone name for this"
              + " upgrade state entry")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private String azName;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Server type (MASTER or TSERVER) for"
              + " this AZ")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private ServerType serverType;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Cluster UUID (primary or read replica)"
              + " for this AZ")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private UUID clusterUUID;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Per-AZ upgrade status: NOT_STARTED;"
              + " IN_PROGRESS before that AZ's upgrade subtasks run, then COMPLETED when done, or"
              + " FAILED on task failure / YBA restart; COMPLETED; FAILED (subtask failure or stale"
              + " IN_PROGRESS after upgrade failure / platform restart)")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  private AZUpgradeStatus status = AZUpgradeStatus.NOT_STARTED;
}
