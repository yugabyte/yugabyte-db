// Copyright (c) YugaByte, Inc.

import { FETCH_TABLES_LIST, FETCH_TABLES_LIST_SUCCESS, FETCH_TABLES_LIST_FAILURE, RESET_TABLES_LIST,
  FETCH_TABLE_DETAIL, FETCH_TABLE_DETAIL_SUCCESS, FETCH_TABLE_DETAIL_FAILURE, RESET_TABLE_DETAIL,
  FETCH_COLUMN_TYPES, FETCH_COLUMN_TYPES_SUCCESS, FETCH_COLUMN_TYPES_FAILURE, TOGGLE_TABLE_VIEW,
  BULK_IMPORT, BULK_IMPORT_RESPONSE
} from '../actions/tables';
import { getInitialState, setLoadingState, setPromiseResponse } from '../utils/PromiseUtils';

const INITIAL_STATE = {
  universeTablesList: [],
  currentTableDetail: {},
  columnDataTypes: {},
  currentTableView: 'list',
  bulkImport: getInitialState({})
};

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
    case FETCH_TABLE_DETAIL:
      return { ...state, currentTableDetail: {}, loading: true, error: null};
    case FETCH_TABLE_DETAIL_SUCCESS:
      return { ...state, currentTableDetail: action.payload.data, loading: false, error: null};
    case FETCH_TABLE_DETAIL_FAILURE:
      return {...state, currentTableDetail: {}, loading: false, error: action.payload.data.error};
    case RESET_TABLE_DETAIL:
      return {...state, currentTableDetail: {}};
    case FETCH_COLUMN_TYPES:
      return {...state};
    case FETCH_COLUMN_TYPES_SUCCESS:
      return {...state, columnDataTypes: action.payload.data};
    case FETCH_COLUMN_TYPES_FAILURE:
      return {...state};
    case TOGGLE_TABLE_VIEW:
      return {...state, currentTableView: action.payload};
    case BULK_IMPORT:
      return setLoadingState(state, "bulkImport", {});
    case BULK_IMPORT_RESPONSE:
      return setPromiseResponse(state, "bulkImport", action);
    default:
      return state;
  }
}
