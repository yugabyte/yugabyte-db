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

export const FETCH_UNIVERSES_PENDING_TASKS = 'FETCH_UNIVERSES_PENDING_TASKS';
export const FETCH_UNIVERSES_PENDING_TASKS_SUCCESS = 'FETCH_UNIVERSES_PENDING_TASKS_SUCCESS';
export const FETCH_UNIVERSES_PENDING_TASKS_FAILURE = 'FETCH_UNIVERSES_PENDING_TASKS_FAILURE';

export const FETCH_PROVIDER_TASKS = 'FETCH_PROVIDER_TASKS';
export const FETCH_PROVIDER_TASKS_SUCCESS = 'FETCH_PROVIDER_TASKS_SUCCESS';
export const FETCH_PROVIDER_TASKS_FAILURE = 'FETCH_PROVIDER_TASKS_FAILURE';

export const FETCH_FAILED_TASK_DETAIL = 'FETCH_TASK_DETAIL';
export const FETCH_FAILED_TASK_DETAIL_RESPONSE = 'FETCH_TASK_DETAIL_RESPONSE';

export const RETRY_TASK = 'RETRY_TASK';
export const RETRY_TASK_RESPONSE = 'RETRY_TASK_RESPONSE';

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

export function resetTaskProgress(error) {
  return {
    type: RESET_TASK_PROGRESS
  };
}

export function fetchCustomerTasks(page, limit) {
  let endpoint = `${getCustomerEndpoint()}/tasks`;
  if (page && limit) {
    endpoint += `?page=${page}&limit=${limit}`;
  }
  const request = axios.get(endpoint);
  return {
    type: FETCH_CUSTOMER_TASKS,
    payload: request
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

/**
 * Fetches pending tasks for all universes of this customer.
 * Query parameters:
 *  `status` [Optional] Can only be "pending"
 *  `target` [Optional] Can be either "universes", or "provider"
 **/
export function fetchUniversesPendingTasks() {
  const request = axios.get(`${getCustomerEndpoint()}/tasks?status=pending&target=universes`);
  return {
    type: FETCH_UNIVERSES_PENDING_TASKS,
    payload: request
  };
}

export function fetchUniversesPendingTasksSuccess(result) {
  return {
    type: FETCH_UNIVERSES_PENDING_TASKS_SUCCESS,
    payload: result
  };
}

export function fetchUniversesPendingTasksFailure(error) {
  return {
    type: FETCH_UNIVERSES_PENDING_TASKS_FAILURE,
    payload: error
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
    payload: response.payload
  };
}

export function fetchProviderTasks(providerUUID) {
  const request = axios.get(`${getCustomerEndpoint()}/providers/${providerUUID}/tasks?limit=1`);
  return {
    type: FETCH_PROVIDER_TASKS,
    payload: request
  };
}

export function fetchProviderTasksSuccess(response) {
  return {
    type: FETCH_PROVIDER_TASKS_SUCCESS,
    payload: response
  };
}

export function fetchProviderTasksFailure(response) {
  return {
    type: FETCH_PROVIDER_TASKS_FAILURE,
    payload: response
  };
}
