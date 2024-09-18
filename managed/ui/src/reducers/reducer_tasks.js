// Copyright (c) YugaByte, Inc.
import {
  FETCH_TASK_PROGRESS,
  FETCH_TASK_PROGRESS_RESPONSE,
  RESET_TASK_PROGRESS,
  FETCH_CUSTOMER_TASKS,
  FETCH_CUSTOMER_TASKS_SUCCESS,
  FETCH_CUSTOMER_TASKS_FAILURE,
  RESET_CUSTOMER_TASKS,
  FETCH_FAILED_TASK_DETAIL,
  FETCH_FAILED_TASK_DETAIL_RESPONSE,
  PATCH_TASKS_FOR_CUSTOMER
} from '../actions/tasks';
import moment from 'moment';

import {
  getInitialState,
  setInitialState,
  setLoadingState,
  setPromiseResponse
} from '../utils/PromiseUtils';

const INITIAL_STATE = {
  taskProgressData: getInitialState({}),
  customerTaskList: [],
  failedTasks: getInitialState([])
};

export default function (state = INITIAL_STATE, action) {
  switch (action.type) {
    case FETCH_TASK_PROGRESS:
      return setLoadingState(state, 'taskProgressData', {});
    case FETCH_TASK_PROGRESS_RESPONSE:
      return setPromiseResponse(state, 'taskProgressData', action);
    case RESET_TASK_PROGRESS:
      return setInitialState(state, 'taskProgressData', {});
    case FETCH_CUSTOMER_TASKS:
      return { ...state };
    case FETCH_CUSTOMER_TASKS_SUCCESS: {
      const taskData = action.payload.data;
      const taskListResultArray = [];
      Object.keys(taskData).forEach(function (taskIdx) {
        taskData[taskIdx].forEach(function (taskItem) {
          taskItem.targetUUID = taskIdx;
          taskListResultArray.push(taskItem);
        });
      });
      return {
        ...state,
        customerTaskList: taskListResultArray.sort((a, b) => moment(b.createTime).isBefore(a.createTime) ? -1 : 1)
      };
    }
    case FETCH_CUSTOMER_TASKS_FAILURE:
      if ('data' in action.payload) {
        return { ...state, customerTaskList: action.payload.response.data.error };
      }
      // If network call fails to complete and returns an error
      return { ...state };
    case FETCH_FAILED_TASK_DETAIL:
      return setLoadingState(state, 'failedTasks', []);
    case FETCH_FAILED_TASK_DETAIL_RESPONSE:
      return setPromiseResponse(state, 'failedTasks', action);
    case RESET_CUSTOMER_TASKS:
      return { ...state, customerTaskList: [] };
    case PATCH_TASKS_FOR_CUSTOMER:
      return {
        ...state,
        customerTaskList: state.customerTaskList
        .filter((task) => task.targetUUID !== action.payload.universeUUID)
        .concat(action.payload.tasks)
        .sort((a, b) => moment(b.createTime).isBefore(a.createTime) ? -1 : 1)
      };
    default:
      return state;
  }
}
