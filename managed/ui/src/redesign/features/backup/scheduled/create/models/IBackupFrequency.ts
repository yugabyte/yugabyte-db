/*
 * Created on Thu Aug 08 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { IBackupSchedule } from '../../../../../../components/backupv2';

// backup strategy type
// Point-in-time is only supported bny the new API - create_backup_schedule_async
export enum BackupStrategyType {
  STANDARD = 'Standard',
  POINT_IN_TIME = 'Point-in-time'
}
export enum TimeUnit {
  MINUTES = 'Minutes',
  HOURS = 'Hours',
  DAYS = 'Days',
  MONTHS = 'Months',
  YEARS = 'Years'
}

export interface BackupFrequencyModel
  extends Pick<
    IBackupSchedule,
    | 'frequency'
    | 'cronExpression'
    | 'incrementalBackupFrequency'
    | 'incrementalBackupFrequencyTimeUnit'
  > {
  backupStrategy: BackupStrategyType;
  useCronExpression: boolean;
  frequencyTimeUnit: TimeUnit;
  useIncrementalBackup: boolean;
  expiryTime: number;
  expiryTimeUnit: TimeUnit;
  keepIndefinitely: boolean; //don't delete backup
}
