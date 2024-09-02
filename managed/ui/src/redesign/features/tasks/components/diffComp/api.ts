/*
 * Created on Tue Jun 04 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../../../config';
import { AuditLogProps, DiffApiResp } from './dtos';

/**
 * Retrieves the diff details for a specific task.
 * @param taskUUID - The UUID of the task.
 * @returns DiffApiResp.
 */
export const getTaskDiffDetails = (taskUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/tasks/${taskUUID}/diff_details`;
  return axios.get<DiffApiResp>(requestUrl);
};

export const getAuditLog = (taskUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/tasks/${taskUUID}/audit_info`;
  return axios.get<AuditLogProps>(requestUrl);
};
