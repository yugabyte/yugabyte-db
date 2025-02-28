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
  SHOW_TASK_BANNER,
  HIDE_TASK_BANNER
} from '../actions/tasks';
import moment from 'moment';
import { get, set } from 'lodash';

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
      const taskAbsent = state.customerTaskList.filter(task => !taskMap[task.id]);
      taskListResultArray.push(...taskAbsent);
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
    case SHOW_TASK_IN_DRAWER:
      return { ...state, showTaskInDrawer: action.payload };
    case HIDE_TASK_IN_DRAWER:
      return { ...state, showTaskInDrawer: '' };
    case SHOW_TASK_BANNER: {
      const bannerInfos = {
        ...state.taskBannerInfo,
      };
      const alreadyExists = get(bannerInfos, [action.payload.universeUUID, action.payload.taskUUID]);
      if (!alreadyExists || alreadyExists.visible !== false) {
        set(bannerInfos, [action.payload.universeUUID, action.payload.taskUUID], {
          visible: true,
          timestamp: Date.now(),
          taskUUID: action.payload.taskUUID,
        });
      }
      return { ...state, taskBannerInfo: bannerInfos };
    }
    case HIDE_TASK_BANNER:
      set(state.taskBannerInfo, [action.payload.universeUUID, action.payload.taskUUID, 'visible'], false);
      return {
        ...state,
      };
    default:
      return state;
  }
}
