// Copyright (c) YugaByte, Inc.
import {
  FETCH_TASK_PROGRESS,
  FETCH_TASK_PROGRESS_RESPONSE,
  RESET_TASK_PROGRESS,
  FETCH_CUSTOMER_TASKS,
  FETCH_CUSTOMER_TASKS_SUCCESS,
  FETCH_CUSTOMER_TASKS_FAILURE,
  FETCH_UNIVERSES_PENDING_TASKS,
  FETCH_UNIVERSES_PENDING_TASKS_SUCCESS,
  FETCH_UNIVERSES_PENDING_TASKS_FAILURE,
  RESET_CUSTOMER_TASKS,
  FETCH_FAILED_TASK_DETAIL,
  FETCH_FAILED_TASK_DETAIL_RESPONSE,
  FETCH_PROVIDER_TASKS,
  FETCH_PROVIDER_TASKS_SUCCESS,
  FETCH_PROVIDER_TASKS_FAILURE
} from '../actions/tasks';

import {
  FETCH_UNIVERSE_PENDING_TASKS,
  FETCH_UNIVERSE_PENDING_TASKS_RESPONSE
} from '../actions/universe';

import {
  getInitialState,
  setInitialState,
  setLoadingState,
  setPromiseResponse,
  setFailureState,
  setSuccessState,
  getPromiseState
} from '../utils/PromiseUtils';

const INITIAL_STATE = {
  taskProgressData: getInitialState({}),
  customerTaskList: [],
  failedTasks: getInitialState([]),
  universesPendingTasks: getInitialState({}),
  providerTasks: getInitialState([])
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
      const taskResponse = action.payload.data;
      const { data, items } = taskResponse;
      const newTasksList = []; //...state.customerTaskList
      // Create map of task ids to array indexes
      const newTasksMap = {};
      state.customerTaskList.forEach((task, index) => {
        newTasksMap[task.id] = index;
      });
      Object.keys(data).forEach((taskIdx) => {
        data[taskIdx].forEach((taskItem) => {
          taskItem.targetUUID = taskIdx;
          // Update existing task or add to list
          if (taskItem.id in newTasksMap) {
            newTasksList[newTasksMap[taskItem.id]] = taskItem;
          } else {
            newTasksList.push(taskItem);
          }
        });
      });
      return {
        ...state,
        customerTaskList: newTasksList.sort((a, b) => b.createTime - a.createTime),
        taskPagination: {
          items
        }
      };
    }
    case FETCH_CUSTOMER_TASKS_FAILURE:      
      if ('data' in action.payload) {
        return { ...state, customerTaskList: action.payload.response.data.error };
      }
      // If network call fails to complete and returns an error
      return { ...state };
    case FETCH_UNIVERSE_PENDING_TASKS: {
      if (getPromiseState(state.universesPendingTasks).isSuccess()) {
        const pendingTasksData = { ...state.universesPendingTasks.data };
        if (action.target) {
          // DELETE current universe's task data
          delete pendingTasksData[action.target];
        }
        return {
          ...state,
          universesPendingTasks: {
            ...state.universesPendingTasks,
            data: pendingTasksData
          }
        };
      } else {
        return setLoadingState(state, 'universesPendingTasks', {});
      }
    }
    case FETCH_UNIVERSE_PENDING_TASKS_RESPONSE: {
      const data = { ...state.universesPendingTasks.data, ...action.payload.data };
      return setSuccessState(state, 'universesPendingTasks', data);
    }
    case FETCH_UNIVERSES_PENDING_TASKS:
      return setLoadingState(state, 'universesPendingTasks', {});
    case FETCH_UNIVERSES_PENDING_TASKS_SUCCESS:
      return setPromiseResponse(state, 'universesPendingTasks', action);
    case FETCH_UNIVERSES_PENDING_TASKS_FAILURE:
      return setInitialState(state, 'universesPendingTasks', {});
    case FETCH_FAILED_TASK_DETAIL:
      return setLoadingState(state, 'failedTasks', []);
    case FETCH_FAILED_TASK_DETAIL_RESPONSE:
      return setPromiseResponse(state, 'failedTasks', action);
    case FETCH_PROVIDER_TASKS:
      return setLoadingState(state, 'providerTasks', {});
    case FETCH_PROVIDER_TASKS_SUCCESS:
      return setPromiseResponse(state, 'providerTasks', action);
      case FETCH_PROVIDER_TASKS_FAILURE:
      return setFailureState(state, 'providerTasks', action);
    case RESET_CUSTOMER_TASKS:
      return { ...state, customerTaskList: [], taskPagination: {} };
    default:
      return state;
  }
}
