// Copyright (c) YugaByte, Inc.

import { GET_REGION_LIST, GET_REGION_LIST_SUCCESS, GET_REGION_LIST_FAILURE,
  GET_PROVIDER_LIST, GET_PROVIDER_LIST_SUCCESS, GET_PROVIDER_LIST_FAILURE,
  GET_INSTANCE_TYPE_LIST, GET_INSTANCE_TYPE_LIST_SUCCESS, GET_INSTANCE_TYPE_LIST_FAILURE,
  RESET_PROVIDER_LIST, GET_SUPPORTED_REGION_DATA, GET_SUPPORTED_REGION_DATA_SUCCESS,
  GET_SUPPORTED_REGION_DATA_FAILURE, CREATE_PROVIDER, CREATE_PROVIDER_SUCCESS,
  CREATE_PROVIDER_FAILURE, CREATE_REGION, CREATE_REGION_SUCCESS, CREATE_REGION_FAILURE,
  CREATE_ACCESS_KEY, CREATE_ACCESS_KEY_SUCCESS, CREATE_ACCESS_KEY_FAILURE,
  INITIALIZE_PROVIDER, INITIALIZE_PROVIDER_SUCCESS, INITIALIZE_PROVIDER_FAILURE,
  DELETE_PROVIDER, DELETE_PROVIDER_SUCCESS, DELETE_PROVIDER_FAILURE, RESET_PROVIDER_BOOTSTRAP,
  LIST_ACCESS_KEYS, LIST_ACCESS_KEYS_SUCCESS, LIST_ACCESS_KEYS_FAILURE,
  CREATE_ONPREM_PROVIDER_RESPONSE, GET_PROVIDER_LIST_RESPONSE, GET_EBS_TYPE_LIST,
  GET_EBS_TYPE_LIST_RESPONSE, CREATE_DOCKER_PROVIDER, CREATE_DOCKER_PROVIDER_RESPONSE,
  FETCH_CLOUD_METADATA
} from '../actions/cloud';

import { setSuccessState, setFailureState, setLoadingState, setResponseState }  from './common';
import _ from 'lodash';

const INITIAL_STATE = {
  regions: [],
  providers: [],
  instanceTypes: [],
  ebsTypes: [],
  loading: {
    regions: false,
    providers: false,
    instanceTypes: false,
    ebsTypes: true,
    supportedRegions: false
  },
  selectedProvider: null,
  error: null,
  accessKeys: {},
  supportedRegionList: [],
  bootstrap: {},
  dockerBootstrap: {},
  status : 'init',
  createOnPremSucceeded: false,
  createOnPremFailed: false,
  fetchMetadata: false
};

export default function(state = INITIAL_STATE, action) {
  let error;
  let success;
  switch(action.type) {
    case GET_PROVIDER_LIST:
      return {...state, providers: [], status: 'storage', error: null, fetchMetadata: false, loading: _.assign(state.loading, {providers: true})};
    case GET_PROVIDER_LIST_SUCCESS:
      return {...state, fetchMetadata: false, providers: _.sortBy(action.payload, "name"), status: 'provider_fetch_success',
        error: null, loading: _.assign(state.loading, {providers: false})};
    case GET_PROVIDER_LIST_FAILURE:
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, providers: null, status: 'provider_fetch_failure', error: error, loading: _.assign(state.loading,
        {providers: false}), fetchMetadata: false};
    case GET_PROVIDER_LIST_RESPONSE:
      if (action.payload.status !== 200 ) {
        // getProviderList failed
        error = action.payload.data || {message: action.payload.message}; // 2nd is network or server down errors
        return { ...state, providers: null, status: 'provider_fetch_failure', error: error,
          loading: _.assign(state.loading, {providers: false}), fetchMetadata: false};
      } else {
        // getProviderList succeeded
        return {...state, providers: _.sortBy(action.payload.data, "name"), status: 'provider_fetch_success',
          error: null, loading: _.assign(state.loading, {providers: false}), fetchMetadata: false};
      }
    case GET_REGION_LIST:
      return { ...state, regions: [], status: 'storage', error: null, loading: _.assign(state.loading, {regions: true})};
    case GET_REGION_LIST_SUCCESS:
      return { ...state, regions: _.sortBy(action.payload.data, "name"), status: 'region_fetch_success',
        error: null, loading: _.assign(state.loading, {regions: false})};
    case GET_REGION_LIST_FAILURE:// return error and make loading = false
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, regions: null, status: 'region_fetch_failure', error: error, loading: _.assign(state.loading, {regions: false})};
    case GET_INSTANCE_TYPE_LIST:
      return {...state, instanceTypes: [], status: 'storage', error: null, loading: _.assign(state.loading, {instanceTypes: true})};
    case GET_INSTANCE_TYPE_LIST_SUCCESS:
      return {...state, instanceTypes: _.sortBy(action.payload.data, "instanceTypeCode"),
        status: 'region_fetch_success', error: null, loading: _.assign(state.loading, {instanceTypes: false})};
    case GET_INSTANCE_TYPE_LIST_FAILURE:
      error = action.payload.data || {message: action.payload.message};//2nd one is network or server down errors
      return { ...state, instanceTypes: null, status: 'instance_type_fetch_failure',
        error: error, loading: _.assign(state.loading, {instanceTypes: false})};
    case RESET_PROVIDER_LIST:
      return { ...state, providers: [], regions: [], instanceTypes:[], loading: _.assign(state.loading, {instanceTypes: false, providers: false, regions: false, supportedRegions: false})}
    case GET_SUPPORTED_REGION_DATA:
      return { ...state, supportedRegionList: [], status: 'supported_region_fetch', loading: _.assign(state.loading, {supportedRegions: true})};
    case GET_SUPPORTED_REGION_DATA_SUCCESS:
      return {...state, supportedRegionList: action.payload.data, status: 'supported_region_fetch_success', loading: _.assign(state.loading, {supportedRegions: false})}
    case GET_SUPPORTED_REGION_DATA_FAILURE:
      return {...state, supportedRegionList: [], status: 'supported_region_fetch_failure', loading: _.assign(state.loading, {supportedRegions: false})}
    case CREATE_PROVIDER:
      return { ...state, bootstrap: {type: 'provider', response: null, loading: true}};
    case CREATE_PROVIDER_SUCCESS:
      return { ...state, bootstrap: {type: 'provider', response: action.payload.data, loading: false}};
    case CREATE_PROVIDER_FAILURE:
      return { ...state, bootstrap: {type: 'provider', error: action.payload.data.error, loading: false}};
    case CREATE_ONPREM_PROVIDER_RESPONSE:
      success = action.payload.status === 200;
      return {...state, createOnPremSucceeded: success, createOnPremFailed: !success };
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
    case DELETE_PROVIDER:
      return { ...state, bootstrap: {type: 'cleanup', response: null, loading: true}};
    case DELETE_PROVIDER_SUCCESS:
      return { ...state, bootstrap: {type: 'cleanup', response: action.payload.data, loading: false}};
    case DELETE_PROVIDER_FAILURE:
      return { ...state, bootstrap: {type: 'cleanup', error: action.payload.data.error, loading: false}};
    case RESET_PROVIDER_BOOTSTRAP:
      return { ...state, bootstrap: {}};
    case LIST_ACCESS_KEYS:
      return setLoadingState(state, "accessKeys");
    case LIST_ACCESS_KEYS_SUCCESS:
      return setSuccessState(state, "accessKeys", action.payload.data);
    case LIST_ACCESS_KEYS_FAILURE:
      return setFailureState(state, "accessKeys", action.payload.data.error);
    case GET_EBS_TYPE_LIST:
      return {...state, ebsTypes: [], status: 'storage', error: null, loading: _.assign(state.loading, {ebsTypes: true})};
    case GET_EBS_TYPE_LIST_RESPONSE:
      if (action.payload.status === 200)
        return { ...state, ebsTypes: action.payload.data, loading: _.assign(state.loading, {ebsTypes: false})};
      return { ...state, ebsTypes: [], error: error, loading: _.assign(state.loading, {ebsTypes: false})};
    case CREATE_DOCKER_PROVIDER:
      return setLoadingState(state, "dockerBootstrap", {});
    case CREATE_DOCKER_PROVIDER_RESPONSE:
      return setResponseState(state, "dockerBootstrap", action);
    case FETCH_CLOUD_METADATA:
      return {...state, fetchMetadata: true};
    default:
      return state;
  }
}
