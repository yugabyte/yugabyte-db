// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

@Data
@ApiModel(
    description =
        "WARNING: This is a preview API that could change."
            + "Information about suggested number of servers to roll at a time")
public class RollMaxBatchSize {
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change."
              + "Suggested number nodes for primary cluster")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.0.0")
  private Integer primaryBatchSize = 1;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change."
              + "Suggested number nodes for readonly cluster")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.0.0")
  private Integer readReplicaBatchSize = 1;

  public static RollMaxBatchSize of(int primaryBatchSize, int readReplicaBatchSize) {
    RollMaxBatchSize rollMaxBatchSize = new RollMaxBatchSize();
    rollMaxBatchSize.primaryBatchSize = primaryBatchSize;
    rollMaxBatchSize.readReplicaBatchSize = readReplicaBatchSize;
    return rollMaxBatchSize;
  }
}
