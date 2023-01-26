// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.models;

import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class RestoreResp {
  UUID restoreUUID;
  UUID universeUUID;
  UUID sourceUniverseUUID;
  UUID customerUUID;
  String targetUniverseName;
  String sourceUniverseName;
  Date createTime;
  Date updateTime;
  Restore.State state;
  long restoreSizeInBytes;
  List<RestoreKeyspace> restoreKeyspaceList;
  Boolean isSourceUniversePresent;
}
