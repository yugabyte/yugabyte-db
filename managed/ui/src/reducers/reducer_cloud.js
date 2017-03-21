// Copyright (c) YugaByte, Inc.

import { GET_REGION_LIST, GET_REGION_LIST_SUCCESS, GET_REGION_LIST_FAILURE,
 GET_PROVIDER_LIST, GET_PROVIDER_LIST_SUCCESS, GET_PROVIDER_LIST_FAILURE,
  GET_INSTANCE_TYPE_LIST, GET_INSTANCE_TYPE_LIST_SUCCESS, GET_INSTANCE_TYPE_LIST_FAILURE,
  RESET_PROVIDER_LIST, GET_SUPPORTED_REGION_DATA, GET_SUPPORTED_REGION_DATA_SUCCESS,
  GET_SUPPORTED_REGION_DATA_FAILURE, CREATE_PROVIDER, CREATE_PROVIDER_SUCCESS,
  CREATE_PROVIDER_FAILURE, CREATE_REGION, CREATE_REGION_SUCCESS, CREATE_REGION_FAILURE,
  CREATE_ACCESS_KEY, CREATE_ACCESS_KEY_SUCCESS, CREATE_ACCESS_KEY_FAILURE,
  INITIALIZE_PROVIDER, INITIALIZE_PROVIDER_SUCCESS, INITIALIZE_PROVIDER_FAILURE } from '../actions/cloud';

import _ from 'lodash';

const INITIAL_STATE = {regions: [], providers: [], instanceTypes: [],
  selectedProvider: null, error: null, supportedRegionList: [], bootstrap: {}};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case GET_PROVIDER_LIST:
      return {...state, providers: [], status: 'storage', error: null, loading: true};
    case GET_PROVIDER_LIST_SUCCESS:
      return {...state, providers: _.sortBy(action.payload, "name"), status: 'provider_fetch_success', error: null, loading:false};
    case GET_PROVIDER_LIST_FAILURE:
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, providers: null, status: 'provider_fetch_failure', error: error, loading: false};
    case GET_REGION_LIST:
      return { ...state, regions: [], status: 'storage', error: null, loading: true};
    case GET_REGION_LIST_SUCCESS:
      return { ...state, regions: _.sortBy(action.payload.data, "name"), status: 'region_fetch_success', error: null, loading: false}; //<-- authenticated
    case GET_REGION_LIST_FAILURE:// return error and make loading = false
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, regions: null, status: 'region_fetch_failure', error: error, loading: false};
    case GET_INSTANCE_TYPE_LIST:
      return {...state, instanceTypes: [], status: 'storge', error: null, loading: true};
    case GET_INSTANCE_TYPE_LIST_SUCCESS:
      return {...state, instanceTypes: _.sortBy(action.payload.data, "instanceTypeCode"), status: 'region_fetch_success', error: null, loading: false};
    case GET_INSTANCE_TYPE_LIST_FAILURE:
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, instanceTypes: null, status: 'instance_type_fetch_failure', error: error, loading: false};
    case RESET_PROVIDER_LIST:
      return { ...state, providers: [], regions: [], instanceTypes:[]}
    case GET_SUPPORTED_REGION_DATA:
      return { ...state, supportedRegionList: [], status: 'supported_region_fetch'};
    case GET_SUPPORTED_REGION_DATA_SUCCESS:
      return {...state, supportedRegionList: action.payload.data, status: 'supported_region_fetch_success'}
    case GET_SUPPORTED_REGION_DATA_FAILURE:
      return {...state, supportedRegionList: [], status: 'supported_region_fetch_failure'}
    case CREATE_PROVIDER:
      return { ...state, bootstrap: {type: 'provider', response: null, loading: true}};
    case CREATE_PROVIDER_SUCCESS:
      return { ...state, bootstrap: {type: 'provider', response: action.payload.data, loading: false}};
    case CREATE_PROVIDER_FAILURE:
      return { ...state, bootstrap: {type: 'provider', error: action.payload.data.error, loading: false}};
    case CREATE_REGION:
      return { ...state, bootstrap: {type: 'region', response: null, loading: true}};
    case CREATE_REGION_SUCCESS:
      return { ...state, bootstrap: {type: 'region', response: action.payload.data, loading: false}};
    case CREATE_REGION_FAILURE:
      return { ...state, bootstrap: {type: 'region', error: action.payload.data.error, loading: false}};
    case CREATE_ACCESS_KEY:
      return { ...state, bootstrap: {type: 'access-key', response: null, loading: true}};
    case CREATE_ACCESS_KEY_SUCCESS:
      return { ...state, bootstrap: {type: 'access-key', response: action.payload.data, loading: false}};
    case CREATE_ACCESS_KEY_FAILURE:
      return { ...state, bootstrap: {type: 'access-key', error: action.payload.data.error, loading: false}};
    case INITIALIZE_PROVIDER:
      return { ...state, bootstrap: {type: 'initialize', response: null, loading: true}};
    case INITIALIZE_PROVIDER_SUCCESS:
      return { ...state, bootstrap: {type: 'initialize', response: action.payload.data, loading: false}};
    case INITIALIZE_PROVIDER_FAILURE:
      return { ...state, bootstrap: {type: 'initialize', error: action.payload.data.error, loading: false}};
    default:
      return state;
  }
}
