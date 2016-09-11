// Copyright (c) YugaByte, Inc.

import { FETCH_UNIVERSE_INFO, FETCH_UNIVERSE_INFO_SUCCESS, FETCH_UNIVERSE_INFO_FAILURE, RESET_UNIVERSE_INFO,
         CREATE_UNIVERSE, CREATE_UNIVERSE_SUCCESS, CREATE_UNIVERSE_FAILURE,
         FETCH_UNIVERSE_LIST, FETCH_UNIVERSE_LIST_SUCCESS, FETCH_UNIVERSE_LIST_FAILURE,
         RESET_UNIVERSE_LIST, DELETE_UNIVERSE, DELETE_UNIVERSE_SUCCESS,
         DELETE_UNIVERSE_FAILURE, FETCH_UNIVERSE_TASKS, FETCH_UNIVERSE_TASKS_SUCCESS,
         FETCH_UNIVERSE_TASKS_FAILURE, RESET_UNIVERSE_TASKS} from '../actions/universe';


const INITIAL_STATE = {currentUniverse: null, universeList: [], universeTasks: [], error: null};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case CREATE_UNIVERSE:
      return { ...state, loading: true};
    case CREATE_UNIVERSE_SUCCESS:
      return { ...state, loading: false};
    case CREATE_UNIVERSE_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, loading: false, error: error};
    case FETCH_UNIVERSE_INFO:
      return { ...state, loading: true};
    case FETCH_UNIVERSE_INFO_SUCCESS:
      return { ...state, currentUniverse: action.payload.data, error: null, loading: false};
    case FETCH_UNIVERSE_INFO_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, currentUniverse: null, error: error, loading: false};
    case RESET_UNIVERSE_INFO:
      return { ...state, currentUniverse: null, error: null, loading: false};
    case FETCH_UNIVERSE_LIST:
      return { ...state, universeList: [], error: null, loading: true};
    case FETCH_UNIVERSE_LIST_SUCCESS:
      return { ...state, universeList: action.payload.data, error: null, loading: false};
    case FETCH_UNIVERSE_LIST_FAILURE:
      return { ...state, universeList: [], error: error, loading: false};
    case RESET_UNIVERSE_LIST:
      return { ...state, universeList: [], error: null, loading: false};
    case FETCH_UNIVERSE_TASKS:
      return { ...state, universeTasks: [], error: null, loading: true};
    case FETCH_UNIVERSE_TASKS_SUCCESS:
      return { ...state, universeTasks: action.payload.data, error: null, loading: false};
    case FETCH_UNIVERSE_TASKS_FAILURE:
      return { ...state, universeTasks: [], error: error, loading: false};
    case RESET_UNIVERSE_TASKS:
      return { ...state, universeTasks: [], error: null, loading: false};
    case DELETE_UNIVERSE:
      return { ...state, loading: true, error: null };
    case DELETE_UNIVERSE_SUCCESS:
      return { ...state, currentUniverse: null, error: null};
    case DELETE_UNIVERSE_FAILURE:
      return { ...state, error: action.payload.error}
    default:
      return state;
  }
}
