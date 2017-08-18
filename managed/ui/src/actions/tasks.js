// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

export const FETCH_TASK_PROGRESS = 'FETCH_TASK_PROGRESS';
export const FETCH_TASK_PROGRESS_RESPONSE = 'FETCH_TASK_PROGRESS_RESPONSE';
export const RESET_TASK_PROGRESS = 'RESET_TASK_PROGRESS';
export const FETCH_CUSTOMER_TASKS = 'FETCH_CUSTOMER_TASKS';
export const FETCH_CUSTOMER_TASKS_SUCCESS = 'FETCH_CUSTOMER_TASKS_SUCCESS';
export const FETCH_CUSTOMER_TASKS_FAILURE = 'FETCH_CUSTOMER_TASKS_FAILURE';
export const RESET_CUSTOMER_TASKS = 'RESET_CUSTOMER_TASKS';

export function fetchTaskProgress(taskUUID) {
  const customerUUID = localStorage.getItem("customer_id");
  const request =
    axios.get(`${ROOT_URL}/customers/${customerUUID}/tasks/${taskUUID}`);
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

export function fetchCustomerTasks() {
  const customerUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${customerUUID}/tasks`);
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
