/*
 * Created on Tue Jan 09 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../../../config';
import { FailedTask } from '../../dtos';

/**
 * Get the details of the failed task.
 * @param taskUUID The UUID of the task.
 * @returns The details of the failed task.
 */

export const getFailedTaskDetails = (taskUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/tasks/${taskUUID}/failed`;
  return axios.get<FailedTask>(requestUrl);
};

/**
 * Retry the failed tasks.
 * @param taskUUID The UUID of the task.
 * @returns The response of the retry task API.
 */

export const retryTasks = (taskUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/tasks/${taskUUID}`);
};

/**
 * Abort the task.
 * @param taskUUID The UUID of the task.
 * @returns The response of the abort task API.
 */
export const abortTask = (taskUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/tasks/${taskUUID}/abort`);
};
