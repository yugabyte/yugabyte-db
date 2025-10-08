// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.backuprestore;

import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import lombok.AllArgsConstructor;
import lombok.NoArgsConstructor;

@AllArgsConstructor
@NoArgsConstructor
public class BackupPointInTimeRestoreWindow {
  public long timestampRetentionWindowStartMillis;
  public long timestampRetentionWindowEndMillis;

  public BackupPointInTimeRestoreWindow(YbcBackupResponse.RestorableWindow restorableWindow) {
    this(restorableWindow, 0L /* scheduleBackupRetention */);
  }

  public BackupPointInTimeRestoreWindow(
      YbcBackupResponse.RestorableWindow restorableWindow, long scheduleBackupRetention) {
    this.timestampRetentionWindowEndMillis = restorableWindow.getTimestampSnapshotCreationMillis();
    this.timestampRetentionWindowStartMillis =
        restorableWindow.getTimestampSnapshotCreationMillis()
            - (scheduleBackupRetention == 0L
                ? restorableWindow.getTimestampHistoryRetentionMillis()
                : Math.min(
                    restorableWindow.getTimestampHistoryRetentionMillis(),
                    scheduleBackupRetention));
  }
}
