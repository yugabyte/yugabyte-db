// Copyright (c) YugaByte, Inc.
import { FETCH_TASK_PROGRESS, FETCH_TASK_PROGRESS_SUCCESS,
  FETCH_TASK_PROGRESS_FAILURE, RESET_TASK_PROGRESS } from '../actions/tasks';

const INITIAL_STATE = {taskProgressData: []};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case FETCH_TASK_PROGRESS:
      return { ...state, taskProgressData: [], loading: true};
    case FETCH_TASK_PROGRESS_SUCCESS:
      return { ...state, taskProgressData: action.payload.data, error: null, loading: false};
    case FETCH_TASK_PROGRESS_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, taskProgressData: [], error: error, loading: false};
    case RESET_TASK_PROGRESS:
      return { ...state, taskProgressData: [], error: null, loading: false};
    default:
      return state;
  }
}
