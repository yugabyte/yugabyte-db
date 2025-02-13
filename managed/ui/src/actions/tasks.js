// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { getCustomerEndpoint } from './common';
export const FETCH_TASK_PROGRESS = 'FETCH_TASK_PROGRESS';
export const FETCH_TASK_PROGRESS_RESPONSE = 'FETCH_TASK_PROGRESS_RESPONSE';
export const RESET_TASK_PROGRESS = 'RESET_TASK_PROGRESS';
export const FETCH_CUSTOMER_TASKS = 'FETCH_CUSTOMER_TASKS';
export const FETCH_CUSTOMER_TASKS_SUCCESS = 'FETCH_CUSTOMER_TASKS_SUCCESS';
export const FETCH_CUSTOMER_TASKS_FAILURE = 'FETCH_CUSTOMER_TASKS_FAILURE';
export const RESET_CUSTOMER_TASKS = 'RESET_CUSTOMER_TASKS';

export const FETCH_FAILED_TASK_DETAIL = 'FETCH_TASK_DETAIL';
export const FETCH_FAILED_TASK_DETAIL_RESPONSE = 'FETCH_TASK_DETAIL_RESPONSE';

export const RETRY_TASK = 'RETRY_TASK';
export const RETRY_TASK_RESPONSE = 'RETRY_TASK_RESPONSE';

export const ROLLBACK_TASK = 'ROLLBACK_TASK';
export const ROLLBACK_TASK_RESPONSE = 'ROLLBACK_TASK_RESPONSE';

export const ABORT_TASK = 'ABORT_TASK';
export const ABORT_TASK_RESPONSE = 'ABORT_TASK_RESPONSE';

export const PATCH_TASKS_FOR_CUSTOMER = 'PATCH_TASKS_FOR_CUSTOMER';

export const SHOW_TASK_IN_DRAWER = 'SHOW_TASK_IN_DRAWER';
export const HIDE_TASK_IN_DRAWER = 'HIDE_TASK_IN_DRAWER';

export const SHOW_TASK_BANNER = 'SHOW_TASK_BANNER';
export const HIDE_TASK_BANNER = 'HIDE_TASK_BANNER';

export function fetchTaskProgress(taskUUID) {
  const request = axios.get(`${getCustomerEndpoint()}/tasks/${taskUUID}`);
  return {
    type: FETCH_TASK_PROGRESS,
    payload: request
  };
}

export function fetchTaskProgressResponse(result) {
  return {
    type: FETCH_TASK_PROGRESS_RESPONSE,
    payload: result
  };
}

export function resetTaskProgress() {
  return {
    type: RESET_TASK_PROGRESS
  };
}

export function fetchCustomerTasks() {
  const request = axios.get(`${getCustomerEndpoint()}/tasks`);
  return {
    type: FETCH_CUSTOMER_TASKS,
    payload: request
  };
}

/**
 * used to patch a particular universe's tasks to the list of tasks
 */
export function patchTasksForCustomer(universeUUID, tasks) {
  return {
    type: PATCH_TASKS_FOR_CUSTOMER,
    payload: {
      universeUUID,
      tasks
    }
  };
}

export function fetchCustomerTasksSuccess(result) {
  return {
    type: FETCH_CUSTOMER_TASKS_SUCCESS,
    payload: result
  };
}

export function fetchCustomerTasksFailure(error) {
  return {
    type: FETCH_CUSTOMER_TASKS_FAILURE,
    payload: error
  };
}

export function resetCustomerTasks() {
  return {
    type: RESET_CUSTOMER_TASKS
  };
}

export function fetchFailedSubTasks(taskUUID) {
  const request = axios.get(`${getCustomerEndpoint()}/tasks/${taskUUID}/failed`);
  return {
    type: FETCH_FAILED_TASK_DETAIL,
    payload: request
  };
}

export function fetchFailedSubTasksResponse(response) {
  return {
    type: FETCH_FAILED_TASK_DETAIL_RESPONSE,
    payload: response.payload
  };
}

export function retryTask(taskUUID) {
  const request = axios.post(`${getCustomerEndpoint()}/tasks/${taskUUID}`);
  return {
    type: RETRY_TASK,
    payload: request
  };
}

export function retryTaskResponse(response) {
  return {
    type: RETRY_TASK_RESPONSE,
    payload: response
  };
}

export function rollbackTask(taskUUID) {
  const request = axios.post(`${getCustomerEndpoint()}/tasks/${taskUUID}/rollback`);
  return {
    type: ROLLBACK_TASK,
    payload: request
  };
}

export function rollbackTaskResponse(response) {
  return {
    type: ROLLBACK_TASK_RESPONSE,
    payload: response
  };
}

export function abortTask(taskUUID) {
  const request = axios.post(`${getCustomerEndpoint()}/tasks/${taskUUID}/abort`);
  return {
    type: ABORT_TASK,
    payload: request
  };
}

export function abortTaskResponse(response) {
  return {
    type: ABORT_TASK_RESPONSE,
    payload: response
  };
}

export const showTaskInDrawer = (taskUUID) => {
  return {
    type: SHOW_TASK_IN_DRAWER,
    payload: taskUUID
  };
};

export const hideTaskInDrawer = () => {
  return {
    type: HIDE_TASK_IN_DRAWER
  };
};

export const showTaskBanner = (taskUUID, universeUUID) => {
  return {
    type: SHOW_TASK_BANNER,
    payload: { taskUUID, universeUUID }
  };
};

export const hideTaskBanner = (taskUUID, universeUUID) => {
  return {
    type: HIDE_TASK_BANNER,
    payload: { taskUUID, universeUUID }
  };
};
