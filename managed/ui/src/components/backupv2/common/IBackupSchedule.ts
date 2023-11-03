/*
 * Created on Thu Mar 31 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { IBackup } from './IBackup';

interface ScheduleTaskParams {
  firstTry: boolean;
  encryptionAtRestConfig: {
    encryptionAtRestEnabled: boolean;
  };
  timeBeforeDelete: number;
  minNumBackupsToRetain: number;
  schedulingFrequency: number;
  useTablespaces: boolean;
  parallelism: number;
  tableUUIDList: string[];
  universeUUID: string;
  fullBackup: boolean;
  backupType: IBackup['backupType'];
  sse: IBackup['commonBackupInfo']['sse'];
  storageConfigUUID: IBackup['commonBackupInfo']['storageConfigUUID'];
  keyspaceList: IBackup['commonBackupInfo']['responseList'];
  isTableByTableBackup: IBackup['isTableByTableBackup']
  expiryTimeUnit: string;
}

export enum IBackupScheduleStatus {
  ACTIVE = 'Active',
  STOPPED = 'Stopped',
  PAUSED = 'Paused'
}
export interface IBackupSchedule extends Pick<IBackup, 'customerUUID' | 'universeUUID'> {
  scheduleUUID: string;
  taskType: 'BackupUniverse' | 'MultiTableBackup';
  failureCount: number;
  frequency: number;
  runningState: boolean;
  cronExpression: string;
  status: IBackupScheduleStatus;
  backupInfo: ScheduleTaskParams;
  scheduleName: string;
  prevCompletedTask: number;
  nextExpectedTask: number;
  frequencyTimeUnit: string;
  incrementalBackupFrequency: number;
  incrementalBackupFrequencyTimeUnit: string;
  tableByTableBackup: boolean;
}
