// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

export const FETCH_TASK_PROGRESS = 'FETCH_TASK_PROGRESS';
export const FETCH_TASK_PROGRESS_SUCCESS = 'FETCH_TASK_PROGRESS_SUCCESS';
export const FETCH_TASK_PROGRESS_FAILURE = 'FETCH_TASK_PROGRESS_FAILURE';
export const RESET_TASK_PROGRESS = 'RESET_TASK_PROGRESS';

export function fetchTaskProgress(taskUUID) {
  var customerUUID = localStorage.getItem("customer_id");
  const request =
    axios.get(`${ROOT_URL}/customers/${customerUUID}/tasks/${taskUUID}`);
  return {
    type: FETCH_TASK_PROGRESS,
    payload: request
  };
}

export function fetchTaskProgressSuccess(result) {
  return {
    type: FETCH_TASK_PROGRESS_SUCCESS,
    payload: result
  };
}

export function fetchTaskProgressFailure(error) {
  return {
    type: FETCH_TASK_PROGRESS_FAILURE,
    payload: error
  }
}

export function resetTaskProgress(error) {
  return {
    type: RESET_TASK_PROGRESS
  }
}
