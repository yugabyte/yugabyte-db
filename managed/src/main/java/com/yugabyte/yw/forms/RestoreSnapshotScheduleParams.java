package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;

@ApiModel(description = "Restore snapshot schedule parameters")
@NoArgsConstructor
public class RestoreSnapshotScheduleParams extends UniverseTaskParams {

  @ApiModelProperty(value = "Universe UUID")
  @Getter
  @Setter
  private UUID universeUUID;

  @ApiModelProperty(value = "PITR Config UUID")
  public UUID pitrConfigUUID;

  @ApiModelProperty(value = "Restore Time In millis")
  public long restoreTimeInMillis;
}
