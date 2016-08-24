// Copyright (c) YugaByte, Inc.

import { GET_REGION_LIST, GET_REGION_LIST_SUCCESS, GET_REGION_LIST_FAILURE,
 GET_PROVIDER_LIST, GET_PROVIDER_LIST_SUCCESS, GET_PROVIDER_LIST_FAILURE,
  GET_INSTANCE_TYPE_LIST, GET_INSTANCE_TYPE_LIST_SUCCESS, GET_INSTANCE_TYPE_LIST_FAILURE } from '../actions/cloud';

const INITIAL_STATE = {regions: [], providers: [], instanceTypes: [], selectedProvider: null, error: null};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case GET_PROVIDER_LIST:
      return {...state, providers: [], status: 'storage', error: null, loading: true};
    case GET_PROVIDER_LIST_SUCCESS:
      return {...state, providers: action.payload, status: 'provider_fetch_success', error: null, loading:false};
    case GET_PROVIDER_LIST_FAILURE:
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, providers: null, status: 'provider_fetch_failure', error: error, loading: false};
    case GET_REGION_LIST:
      return { ...state, regions: [], status: 'storage', error: null, loading: true};
    case GET_REGION_LIST_SUCCESS:
      return { ...state, regions: action.payload.data, status: 'region_fetch_success', error: null, loading: false}; //<-- authenticated
    case GET_REGION_LIST_FAILURE:// return error and make loading = false
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, regions: null, status: 'region_fetch_failure', error: error, loading: false};
    case GET_INSTANCE_TYPE_LIST:
      return {...state, instanceTypes: [], status: 'storge', error: null, loading: true};
    case GET_INSTANCE_TYPE_LIST_SUCCESS:
      return {...state, instanceTypes: action.payload.data, status: 'region_fetch_success', error: null, loading: false};
    case GET_INSTANCE_TYPE_LIST_FAILURE:
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, instanceTypes: null, status: 'instance_type_fetch_failure', error: error, loading: false};
    default:
      return state;
  }
}
