// Copyright (c) YugaByte, Inc.
import { FETCH_TASK_PROGRESS, FETCH_TASK_PROGRESS_SUCCESS,
         FETCH_TASK_PROGRESS_FAILURE, RESET_TASK_PROGRESS, FETCH_CURRENT_TASK_PROGRESS_FAILURE,
         FETCH_CURRENT_TASK_PROGRESS_SUCCESS, FETCH_CUSTOMER_TASKS, FETCH_CUSTOMER_TASKS_SUCCESS,
         FETCH_CUSTOMER_TASKS_FAILURE, RESET_CUSTOMER_TASKS } from '../actions/tasks';
import _ from 'lodash';

const INITIAL_STATE = {taskProgressData: [], currentTaskList: {}, customerTaskList: []};

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
    case FETCH_CURRENT_TASK_PROGRESS_SUCCESS:
      var taskList = state.currentTaskList;
      taskList[action.payload.taskUUID] = action.payload;
      return { ...state, currentTaskList: taskList, error: null, loading: false};
    case FETCH_CURRENT_TASK_PROGRESS_FAILURE:
      return { ...state}
    case FETCH_CUSTOMER_TASKS:
      return {...state}
    case FETCH_CUSTOMER_TASKS_SUCCESS:
      var taskData = action.payload.data;
      var taskListResultArray = [];
      Object.keys(taskData).forEach(function(taskIdx){
        taskData[taskIdx].forEach(function(taskItem){
          taskListResultArray.push(taskItem);
        })
      });
      return {...state, customerTaskList: _.sortBy(taskListResultArray, "createTime" , ['desc'])}
    case FETCH_CUSTOMER_TASKS_FAILURE:
      return {...state, customerTaskList: action.payload.error}
    case RESET_CUSTOMER_TASKS:
      return {...state, customerTaskList: []};
    default:
      return state;
  }
}
