package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.NoArgsConstructor;
import play.data.validation.Constraints;

@ApiModel(description = "Universe Backup Form Data")
@NoArgsConstructor
@JsonIgnoreProperties(ignoreUnknown = true)
public class UniverseBackupRequestFormData extends AbstractTaskParams {
  @Constraints.Required
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change.Storage configuration UUID",
      required = true)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.2.0")
  public UUID storageConfigUUID;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change.Time before deleting the backup from"
              + " storage, in milliseconds")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.2.0")
  public long timeBeforeDelete = 0L;
}
