/*
 * Created on Wed Aug 14 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { IBackupSchedule } from '../../../../../components/backupv2';
import { ROOT_URL } from '../../../../../config';

/**
 * Creates a scheduled backup.
 *
 * @param payload - The backup information.
 * @returns A promise that resolves to the taskUUID of the backup schedule creation.
 */
export const createScheduledBackupPolicy = (payload: IBackupSchedule['backupInfo']) => {
  const customerUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${customerUUID}/create_backup_schedule_async`, {
    ...payload,
    customerUUID
  });
};

// toggle on/off scheduled backup policy
export const toggleScheduledBackupPolicy = (
  universeUUID: string,
  values: Partial<IBackupSchedule> & Pick<IBackupSchedule, 'scheduleUUID'>
) => {
  const customerUUID = localStorage.getItem('customerId');

  if (values['cronExpression']) {
    delete values['frequency'];
  }

  return axios.put<IBackupSchedule>(
    `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/schedules/${values['scheduleUUID']}/pause_resume`,
    values
  );
};

// delete scheduled backup policy
export const deleteSchedulePolicy = (universeUUID: string, scheduledPolicyUUID: string) => {
  const customerUUID = localStorage.getItem('customerId');

  return axios.delete<IBackupSchedule>(
    `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/schedules/${scheduledPolicyUUID}/delete_backup_schedule_async`
  );
};

// edit scheduled backup policy
export const editScheduledBackupPolicy = (
  universeUUID: string,
  values: Partial<IBackupSchedule> & Pick<IBackupSchedule, 'scheduleUUID'>
) => {
  const customerUUID = localStorage.getItem('customerId');
  return axios.put<IBackupSchedule>(
    `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/schedules/${values['scheduleUUID']}/edit_backup_schedule_async`,
    values
  );
};
