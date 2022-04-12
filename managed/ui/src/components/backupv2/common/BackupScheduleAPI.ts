/*
 * Created on Thu Mar 31 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import moment from 'moment';
import { ROOT_URL } from '../../../config';
import { prepareBackupCreationPayload } from './BackupAPI';
import { IBackupSchedule } from './IBackupSchedule';

type Schedule_List_Reponse = {
  entities: IBackupSchedule[];
  hasNext: boolean;
  hasPrev: boolean;
  totalCount: number;
};

export const getScheduledBackupList = (pageno: number) => {
  const cUUID = localStorage.getItem('customerId');
  const records_to_fetch = 10;
  const params = {
    direction: 'ASC',
    filter: {},
    limit: records_to_fetch,
    offset: pageno * records_to_fetch,
    sortBy: 'taskType'
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
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/create_backup_schedule`;

  const payload = prepareBackupCreationPayload(values, cUUID);

  if (values['use_cron_expression']) {
    payload['cronExpression'] = values['cron_expression'];
  } else {
    payload['schedulingFrequency'] = moment()
      .add(values['policy_interval'], values['policy_interval_type'].value)
      .diff(moment(), 'millisecond');
  }

  return axios.post(requestUrl, payload);
};

export const deleteBackupSchedule = (scheduleUUID: IBackupSchedule['scheduleUUID']) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/schedules/${scheduleUUID}/delete`;
  return axios.delete(requestUrl);
};
