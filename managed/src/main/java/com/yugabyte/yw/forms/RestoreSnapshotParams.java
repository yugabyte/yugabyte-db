package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.NoArgsConstructor;

@ApiModel(description = "Restore snapshot parameters")
@NoArgsConstructor
public class RestoreSnapshotParams extends UniverseTaskParams {

  @ApiModelProperty(value = "Universe UUID")
  public UUID universeUUID;

  @ApiModelProperty(value = "PITR Config UUID")
  public UUID pitrConfigUUID;

  @ApiModelProperty(value = "Snapshot UUID")
  public UUID snapshotUUID;

  @ApiModelProperty(value = "Restore Time In millis")
  public long restoreTimeInMillis;
}
