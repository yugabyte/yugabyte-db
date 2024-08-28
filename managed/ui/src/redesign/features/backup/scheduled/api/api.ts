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
export const createScheduledBackup = (payload: IBackupSchedule['backupInfo']) => {
  const customerUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${customerUUID}/create_backup_schedule_async`, {
    ...payload,
    customerUUID
  });
};
