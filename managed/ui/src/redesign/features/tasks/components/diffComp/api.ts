/*
 * Created on Tue Jun 04 2024
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../../../config';
import { AuditLogProps, DiffApiResp } from './dtos';
import { SubTaskDetailsResp } from '../../dtos';

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

// fetch the details for the parent task by nvaigating through the previousTASKUUID chain
export function fetchRootSubTaskDetails(
  taskUUID: string,
  taskTargetUUID: string
): Promise<SubTaskDetailsResp> {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/tasks/${taskUUID}/details`;
  return new Promise((resolve, reject) => {
    axios
      .get(requestUrl)
      .then((response) => {
        const subTaskDetails = response.data[taskTargetUUID]?.[0]?.subtaskInfos[0]?.taskParams;
        // A task is considered a parent task if it doesn't have a previousTaskUUID
        if (!subTaskDetails.previousTaskUUID) {
          resolve(response.data);
        } else {
          resolve(fetchRootSubTaskDetails(subTaskDetails.previousTaskUUID, taskTargetUUID)); // Recursive call
        }
      })
      .catch((error) => {
        console.error('Error fetching sub-task details:', error);
        reject(error);
      });
  });
}
