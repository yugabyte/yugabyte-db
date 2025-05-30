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
  PATCH_TASKS_FOR_CUSTOMER,
  SHOW_TASK_IN_DRAWER,
  HIDE_TASK_IN_DRAWER,
} from '../actions/tasks';
import moment from 'moment';
import { get, mapValues, set } from 'lodash';

import {
  getInitialState,
  setInitialState,
  setLoadingState,
  setPromiseResponse
} from '../utils/PromiseUtils';

const INITIAL_STATE = {
  taskProgressData: getInitialState({}),
  customerTaskList: [],
  failedTasks: getInitialState([]),
  showTaskInDrawer: '',
  taskBannerInfo: {}
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
      const taskMap = {};

      // We use two stores: Redux and React Query.
      // TaskListTable uses React Query, while the legacy system uses Redux.
      // To keep both in sync, we update the Redux state in TaskListTable via PATCH_TASKS_FOR_CUSTOMER.
      // However, we do not update React Query from Redux.
      // A race condition occurs because Redux issues a /tasks request, which takes time to load as it gathers all tasks from all universes.
      // React Query, on the other hand, fetches only the current universe's tasks, making it faster.
      // If Redux initiates the /tasks request first and React Query follows but completes first, the subsequent Redux response overwrites the data.
      // To prevent this, if the current page is /universe/tasks, we avoid updating the Redux store and let React Query handle the update.
      const currentPath = window.location.pathname;
      const match = currentPath.match(/universes\/([0-9a-fA-F-]+)\/tasks/);
      if (match) {
        const universeUUID = match[1];
        if(taskData[universeUUID])
          delete taskData[universeUUID];
      }
      
      Object.keys(taskData).forEach(function (taskIdx) {
        taskData[taskIdx].forEach(function (taskItem) {
          taskItem.targetUUID = taskIdx;
          taskMap[taskItem.id] = true;
          taskListResultArray.push(taskItem);
        });
      });
      // /tasks api sends max 2000 tasks(unless otherwise configured) for a customer.
      // Patch_For_tasks fetches tasks for the universe and add it to the list of tasks.
      // but, again if this action is called it overrides the previous list of tasks.
      // this causes ui to flicker. so, we are filtering the old tasks and adding the new tasks.
      const taskAbsent = state.customerTaskList.filter((task) => !taskMap[task.id]);
      taskListResultArray.push(...taskAbsent);
      return {
        ...state,
        customerTaskList: taskListResultArray.sort((a, b) =>
          moment(b.createTime).isBefore(a.createTime) ? -1 : 1
        )
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
          .sort((a, b) => (moment(b.createTime).isBefore(a.createTime) ? -1 : 1))
      };
    case SHOW_TASK_IN_DRAWER:
      return { ...state, showTaskInDrawer: action.payload };
    case HIDE_TASK_IN_DRAWER:
      return { ...state, showTaskInDrawer: '' };
    default:
      return state;
  }
}
