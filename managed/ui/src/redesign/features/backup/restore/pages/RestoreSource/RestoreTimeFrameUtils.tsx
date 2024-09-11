/*
 * Created on Wed Aug 28 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { RestoreContext } from '../../models/RestoreContext';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import {
  getFullBackupInIncBackupChain,
  getIncrementalBackupByUUID,
  getLastSuccessfulIncrementalBackup,
  isPITREnabledInBackup
} from '../../RestoreUtils';

type PITRRestoreWindow = {
  from: string | number;
  to: string | number;
  mostRecentbackup: string;
  label: string;
};

export const computePITRRestoreWindow = (
  formValues: RestoreFormModel,
  context: RestoreContext
): PITRRestoreWindow => {
  const { backupDetails, additionalBackupProps, incrementalBackupsData } = context;

  const { currentCommonBackupInfo } = formValues;

  const timeFrame: PITRRestoreWindow = {
    from: '',
    to: '',
    mostRecentbackup: '',
    label: ''
  };

  if (!currentCommonBackupInfo || !backupDetails) return timeFrame;

  // PITR ON , Incremental Backup ON

  if (isPITREnabledInBackup(currentCommonBackupInfo) && backupDetails.hasIncrementalBackups) {
    const lastIncrementalBackup = getLastSuccessfulIncrementalBackup(incrementalBackupsData ?? []);

    // Global restore - Restore Entire Backup
    if (additionalBackupProps?.isRestoreEntireBackup) {
      timeFrame.from =
        lastIncrementalBackup?.responseList[0].backupPointInTimeRestoreWindow
          ?.timestampRetentionWindowStartMillis ?? currentCommonBackupInfo.createTime;
      timeFrame.to =
        lastIncrementalBackup?.responseList[0].backupPointInTimeRestoreWindow
          ?.timestampRetentionWindowEndMillis ?? currentCommonBackupInfo.createTime;
      timeFrame.mostRecentbackup = lastIncrementalBackup!.createTime;
      timeFrame.label = 'mostRecentIncremental';
    }

    if (
      !additionalBackupProps?.isRestoreEntireBackup &&
      additionalBackupProps?.incrementalBackupUUID
    ) {
      const incBackup = additionalBackupProps.singleKeyspaceRestore
        ? currentCommonBackupInfo
        : getIncrementalBackupByUUID(
            incrementalBackupsData ?? [],
            additionalBackupProps.incrementalBackupUUID
          );
      timeFrame.from =
        incBackup?.responseList[0].backupPointInTimeRestoreWindow
          ?.timestampRetentionWindowStartMillis ?? 0;
      timeFrame.to =
        incBackup?.responseList[0].backupPointInTimeRestoreWindow
          ?.timestampRetentionWindowEndMillis ?? 0;
      timeFrame.label = 'backupTime';
      timeFrame.mostRecentbackup = incBackup!.createTime;
    }
  }

  //PITR OFF , Incremental Backup ON

  if (!isPITREnabledInBackup(currentCommonBackupInfo) && backupDetails.hasIncrementalBackups) {
    if (additionalBackupProps?.isRestoreEntireBackup) {
      timeFrame.from =
        getFullBackupInIncBackupChain(incrementalBackupsData ?? [])?.createTime ??
        currentCommonBackupInfo.createTime;
      timeFrame.to =
        getLastSuccessfulIncrementalBackup(incrementalBackupsData ?? [])?.createTime ??
        currentCommonBackupInfo.createTime;
      timeFrame.mostRecentbackup = timeFrame.to;
      timeFrame.label = 'mostRecentIncremental';
    }
  }

  // PITR ON , Incremental Backup OFF

  if (isPITREnabledInBackup(currentCommonBackupInfo) && !backupDetails.hasIncrementalBackups) {
    timeFrame.from =
      currentCommonBackupInfo.responseList[0].backupPointInTimeRestoreWindow
        ?.timestampRetentionWindowStartMillis ?? 0;
    timeFrame.to =
      currentCommonBackupInfo.responseList[0].backupPointInTimeRestoreWindow
        ?.timestampRetentionWindowEndMillis ?? 0;
    timeFrame.label = 'backupTime';
  }

  return timeFrame;
};
