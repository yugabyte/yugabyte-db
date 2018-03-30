// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.UUID;

public class BackupTableParams extends TableManagerParams {
  public enum ActionType {
    CREATE,
    RESTORE
  }

  @Constraints.Required
  public UUID storageConfigUUID;

  // Specifies the backup storage location in case of S3 it would have
  // the S3 url based on universeUUID and timestamp.
  public String storageLocation;

  @Constraints.Required
  public ActionType actionType;
}
