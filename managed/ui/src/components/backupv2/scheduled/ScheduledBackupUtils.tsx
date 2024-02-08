/*
 * Created on Mon Apr 04 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { capitalize, lowerCase } from 'lodash';
import moment from 'moment';
import pluralize from 'pluralize';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { Backup_Options_Type, IStorageConfig } from '../common/IBackup';
import { TableType } from '../../../redesign/helpers/dtos';
import { IBackupSchedule } from '../common/IBackupSchedule';

export const MILLISECONDS_IN = {
  YEARS: 31_536_000_000, //365 days
  MONTHS: 2_629_800_000, //30 days
  DAYS: 86_400_000,
  HOURS: 3_600_000,
  MINUTES: 60_000
};

export const getTimeDurationArr = (msecs: number, dtype: any) => {
  const time = moment.duration(msecs, dtype);
  const timeArray: [number, string][] = [
    [time.seconds(), 'sec'],
    [time.minutes(), 'min'],
    [time.hours(), 'hour'],
    [time.days(), 'day'],
    [time.months(), 'month'],
    [time.years(), 'year']
  ];
  return timeArray.filter((item) => item[0] !== 0).slice(0, 2);
};

export const getReadableTime = (msecs: number, prefix = '', dtype = 'millisecond') => {
  if (msecs === 0) {
    return '-';
  }
  const reducedTimeArray = getTimeDurationArr(msecs, dtype);
  return isDefinedNotNull(reducedTimeArray[1])
    ? `${prefix}${reducedTimeArray[1][0]}
    ${pluralize(reducedTimeArray[1][1], reducedTimeArray[1][0])}
    ${reducedTimeArray[0][0]}
    ${pluralize(reducedTimeArray[0][1], reducedTimeArray[0][0])}`
    : `${prefix}${reducedTimeArray[0][0]}
    ${pluralize(reducedTimeArray[0][1], reducedTimeArray[0][0])}`;
};

export const convertMsecToTimeFrame = (msec: number, dType: string, prefix = '') => {
  const timeFrame = msec / MILLISECONDS_IN[dType];
  return `${prefix} ${timeFrame} ${pluralize(dType, timeFrame).toLowerCase()}`;
};

export const convertScheduleToFormValues = (
  schedule: IBackupSchedule,
  storage_configs: IStorageConfig[]
) => {
  const formValues = {
    scheduleName: schedule.scheduleName,
    use_cron_expression: !!schedule.cronExpression,
    cron_expression: schedule.cronExpression ?? '',
    api_type: {
      value: schedule.backupInfo.backupType,
      label: schedule.backupInfo.backupType === TableType.PGSQL_TABLE_TYPE ? 'YSQL' : 'YCQL'
    },
    selected_ycql_tables: [] as any[],
    keep_indefinitely: schedule.backupInfo.timeBeforeDelete === 0,
    parallel_threads: schedule.backupInfo.parallelism ?? 8,
    scheduleObj: schedule,
    isTableByTableBackup: schedule.tableByTableBackup,
    useTablespaces: schedule.backupInfo.useTablespaces
  };

  if (schedule.backupInfo?.fullBackup) {
    formValues['db_to_backup'] = {
      value: null,
      label: `All ${
        schedule.backupInfo?.backupType === TableType.PGSQL_TABLE_TYPE ? 'Databases' : 'Keyspaces'
      }`
    };
  } else {
    formValues['db_to_backup'] = schedule.backupInfo?.keyspaceList.map((k) => {
      return { value: k.keyspace, label: k.keyspace };
    })[0];

    if (schedule.backupInfo.backupType === TableType.YQL_TABLE_TYPE) {
      formValues['backup_tables'] =
        schedule.backupInfo?.keyspaceList.length > 0 &&
        schedule.backupInfo?.keyspaceList[0].tablesList.length > 0
          ? Backup_Options_Type.CUSTOM
          : Backup_Options_Type.ALL;

      if (formValues['backup_tables'] === Backup_Options_Type.CUSTOM) {
        schedule.backupInfo.keyspaceList.forEach((k: any) => {
          k.tablesList.forEach((table: string, index: number) => {
            formValues['selected_ycql_tables'].push({
              tableUUID: k.tableUUIDList[index],
              tableName: table,
              keySpace: k.keyspace,
              isIndexTable: k.isIndexTable
            });
          });
        });
      }
    }
  }

  if (!schedule.cronExpression) {
    formValues['policy_interval'] =
      schedule.frequency / MILLISECONDS_IN[schedule.frequencyTimeUnit];
    const interval_type = capitalize(lowerCase(schedule.frequencyTimeUnit));
    formValues['policy_interval_type'] = { value: interval_type, label: interval_type };
  }

  if (schedule.incrementalBackupFrequency > 0) {
    formValues['is_incremental_backup_enabled'] = true;
    formValues['incremental_backup_frequency'] =
      schedule.incrementalBackupFrequency /
      MILLISECONDS_IN[schedule.incrementalBackupFrequencyTimeUnit];
    const interval_type = capitalize(lowerCase(schedule.incrementalBackupFrequencyTimeUnit));
    formValues['incremental_backup_frequency_type'] = {
      value: interval_type,
      label: interval_type
    };
  }

  const s_config = storage_configs.find(
    (c) => c.configUUID === schedule.backupInfo.storageConfigUUID
  );

  if (s_config) {
    formValues['storage_config'] = {
      label: s_config.configName,
      value: s_config.configUUID,
      name: s_config.name
    };
  }

  if (schedule.backupInfo.timeBeforeDelete !== 0) {
    formValues['retention_interval'] =
      schedule.backupInfo.timeBeforeDelete / MILLISECONDS_IN[schedule.backupInfo.expiryTimeUnit];
    const interval_type = capitalize(lowerCase(schedule.backupInfo.expiryTimeUnit));
    formValues['retention_interval_type'] = { value: interval_type, label: interval_type };
  }

  return formValues;
};
