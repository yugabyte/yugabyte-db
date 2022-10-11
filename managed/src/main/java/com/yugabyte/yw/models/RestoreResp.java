// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.models;

import java.util.Date;
import java.util.UUID;
import java.util.List;

import org.yb.CommonTypes.TableType;

import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class RestoreResp {
  UUID restoreUUID;
  UUID universeUUID;
  UUID customerUUID;
  String targetUniverseName;
  String sourceUniverseName;
  Date creationTime;
  Date updateTime;
  Restore.State state;
  long restoreSizeInBytes;
  List<RestoreKeyspace> restoreKeyspaceList;
  Boolean isSourceUniversePresent;
}
