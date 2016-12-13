// Copyright (c) YugaByte, Inc.
import { FETCH_TABLES_LIST, FETCH_TABLES_LIST_SUCCESS,
  FETCH_TABLES_LIST_FAILURE, RESET_TABLES_LIST } from '../actions/tables';

const INITIAL_STATE = {universeTablesList: []};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case FETCH_TABLES_LIST:
      return { ...state, universeTablesList: [], loading: true};
    case FETCH_TABLES_LIST_SUCCESS:
      return { ...state, universeTablesList: action.payload.data, error: null, loading: false};
    case FETCH_TABLES_LIST_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, universeTablesList: [], error: error, loading: false};
    case RESET_TABLES_LIST:
      return { ...state, universeTablesList: [], error: null, loading: false};
    default:
      return state;
  }
}
