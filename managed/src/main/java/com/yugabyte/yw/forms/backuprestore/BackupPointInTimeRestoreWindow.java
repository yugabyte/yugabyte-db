// Copyright (c) YugaByte, Inc.

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
    this.timestampRetentionWindowEndMillis = restorableWindow.getTimestampSnapshotCreationMillis();
    this.timestampRetentionWindowStartMillis =
        restorableWindow.getTimestampSnapshotCreationMillis()
            - restorableWindow.getTimestampHistoryRetentionMillis();
  }
}
