// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.List;
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

  public List<String> tableNameList;

  public List<UUID> tableUUIDList;

  // Specifies the frequency for running the backup in milliseconds.
  public long schedulingFrequency = 0L;

  // Specifies the cron expression in case a recurring backup is expected.
  public String cronExpression = null;

  // Specifies the time before deleting the backup from the storage
  // bucket.
  public long timeBeforeDelete = 0L;

  // Should backup script enable verbose logging.
  public boolean enableVerboseLogs = false;

  // Should the backup be transactional across tables
  public boolean transactionalBackup = false;

  // The number of concurrent commands to run on nodes over SSH
  public int parallelism = 8;
}
