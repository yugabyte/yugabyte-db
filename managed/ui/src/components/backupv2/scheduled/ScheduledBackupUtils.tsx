/*
 * Created on Mon Apr 04 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { capitalize } from 'lodash';
import moment from 'moment';
import pluralize from 'pluralize';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { IStorageConfig, TableType } from '../common/IBackup';
import { IBackupSchedule } from '../common/IBackupSchedule';

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
  const reducedTimeArray = getTimeDurationArr(msecs, dtype);

  return isDefinedNotNull(reducedTimeArray[1])
    ? `${prefix}${reducedTimeArray[1][0]}
         ${pluralize(reducedTimeArray[1][1], reducedTimeArray[1][0])}
         ${reducedTimeArray[0][0]}
         ${pluralize(reducedTimeArray[0][1], reducedTimeArray[0][0])}`
    : `${prefix}${reducedTimeArray[0][0]}
         ${pluralize(reducedTimeArray[0][1], reducedTimeArray[0][0])}`;
};

export const convertScheduleToFormValues = (
  schedule: IBackupSchedule,
  storage_configs: IStorageConfig[]
) => {
  const formValues = {
    policy_name: '', // set to empty until api supports ot
    use_cron_expression: !!schedule.cronExpression,
    cron_expression: schedule.cronExpression ?? '',
    api_type: {
      value: schedule.taskParams.backupType,
      label: schedule.taskParams.backupType === TableType.PGSQL_TABLE_TYPE ? 'YSQL' : 'YCQL'
    },
    // backup_tables: Backup_Options_Type.ALL,
    duration_period: 1,
    // duration_type: DURATION_OPTIONS[0],
    selected_ycql_tables: [],
    keep_indefinitely: schedule.taskParams.timeBeforeDelete === 0,
    parallel_threads: schedule.taskParams.parallelism
  };

  if (!schedule.cronExpression) {
    const policyInterval = getTimeDurationArr(schedule.frequency, 'millisecond');
    const i = policyInterval.length - 1;
    formValues['policy_interval'] = policyInterval[i][0];
    formValues['policy_interval_type'] = {
      label: capitalize(policyInterval[i][1]) + 's',
      value: capitalize(policyInterval[i][1]) + 's'
    };
  }

  const s_config = storage_configs.find(
    (c) => c.configUUID === schedule.taskParams.storageConfigUUID
  );

  if (s_config) {
    formValues['storage_config'] = {
      label: s_config.configName,
      value: s_config.configUUID,
      name: s_config.name
    };
  }

  if (schedule.taskParams.timeBeforeDelete !== 0) {
    const retention_duration = getTimeDurationArr(schedule.taskParams.timeBeforeDelete, 'second');
    const j = retention_duration.length - 1;
    formValues['retention_interval'] = retention_duration[j][0];
    formValues['retention_interval_type'] = {
      label: capitalize(retention_duration[j][1]) + 's',
      value: capitalize(retention_duration[j][1]) + 's'
    };
  }

  return formValues;
};
