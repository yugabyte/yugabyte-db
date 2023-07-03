/*
 * Created on Thu Mar 31 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../config';
import { MILLISECONDS_IN } from '../scheduled/ScheduledBackupUtils';
import { prepareBackupCreationPayload } from './BackupAPI';
import { IBackupSchedule } from './IBackupSchedule';

type Schedule_List_Reponse = {
  entities: IBackupSchedule[];
  hasNext: boolean;
  hasPrev: boolean;
  totalCount: number;
};

export const getScheduledBackupList = (pageno: number, universeUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  const records_to_fetch = 500;
  const params = {
    direction: 'ASC',
    filter: {
      taskTypes: ['BackupUniverse', 'MultiTableBackup', 'CreateBackup'],
      universeUUIDList: [universeUUID]
    },
    limit: records_to_fetch,
    offset: pageno * records_to_fetch,
    sortBy: 'scheduleName'
  };
  return axios.post<Schedule_List_Reponse>(`${ROOT_URL}/customers/${cUUID}/schedules/page`, params);
};

export const editBackupSchedule = (
  values: Partial<IBackupSchedule> & Pick<IBackupSchedule, 'scheduleUUID'>
) => {
  const cUUID = localStorage.getItem('customerId');

  if (values['cronExpression']) {
    delete values['frequency'];
  }

  return axios.put<IBackupSchedule[]>(
    `${ROOT_URL}/customers/${cUUID}/schedules/${values['scheduleUUID']}`,
    values
  );
};

export const createBackupSchedule = (values: Record<string, any>) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/create_backup_schedule_async`;

  const payload = prepareBackupCreationPayload(values, cUUID);

  payload['scheduleName'] = values['scheduleName'];

  if (values['use_cron_expression']) {
    payload['cronExpression'] = values['cron_expression'];
  } else {
    payload['schedulingFrequency'] =
      values['policy_interval'] *
      MILLISECONDS_IN[values['policy_interval_type'].value.toUpperCase()];
    payload['frequencyTimeUnit'] = values['policy_interval_type'].value.toUpperCase();
  }

  if (values['is_incremental_backup_enabled']) {
    payload['incrementalBackupFrequency'] =
      values['incremental_backup_frequency'] *
      MILLISECONDS_IN[values['incremental_backup_frequency_type'].value.toUpperCase()];
    payload['incrementalBackupFrequencyTimeUnit'] = values[
      'incremental_backup_frequency_type'
    ].value.toUpperCase();
  }

  return axios.post(requestUrl, payload);
};

export const deleteBackupSchedule = (scheduleUUID: IBackupSchedule['scheduleUUID']) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/schedules/${scheduleUUID}/delete`;
  return axios.delete(requestUrl);
};
