// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import org.yb.CommonTypes.TableType;

@Value
@Builder
public class RestoreResp {
  UUID restoreUUID;
  UUID universeUUID;
  UUID sourceUniverseUUID;
  UUID customerUUID;
  String targetUniverseName;
  String sourceUniverseName;

  @ApiModelProperty(value = "Restore creation time.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date createTime;

  @ApiModelProperty(value = "Restore update time.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date updateTime;

  @ApiModelProperty(value = "Backup details.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date backupCreatedOnDate;

  @ApiModelProperty(required = false)
  TableType backupType;

  Restore.State state;
  long restoreSizeInBytes;
  List<RestoreKeyspace> restoreKeyspaceList;
  Boolean isSourceUniversePresent;
}
