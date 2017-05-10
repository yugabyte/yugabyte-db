// Copyright (c) YugaByte, Inc.

import { GET_REGION_LIST, GET_PROVIDER_LIST,
        GET_INSTANCE_TYPE_LIST, RESET_PROVIDER_LIST, GET_SUPPORTED_REGION_DATA, CREATE_PROVIDER, CREATE_PROVIDER_SUCCESS,
        CREATE_PROVIDER_FAILURE, CREATE_REGION, CREATE_REGION_SUCCESS, CREATE_REGION_FAILURE,
        CREATE_ACCESS_KEY, CREATE_ACCESS_KEY_SUCCESS, CREATE_ACCESS_KEY_FAILURE,
        INITIALIZE_PROVIDER, INITIALIZE_PROVIDER_SUCCESS, INITIALIZE_PROVIDER_FAILURE,
        DELETE_PROVIDER, DELETE_PROVIDER_SUCCESS, DELETE_PROVIDER_FAILURE, RESET_PROVIDER_BOOTSTRAP,
        LIST_ACCESS_KEYS,
        CREATE_ONPREM_PROVIDER_RESPONSE, GET_PROVIDER_LIST_RESPONSE, GET_EBS_TYPE_LIST,
        GET_EBS_TYPE_LIST_RESPONSE, CREATE_DOCKER_PROVIDER, CREATE_DOCKER_PROVIDER_RESPONSE,
        FETCH_CLOUD_METADATA, GET_REGION_LIST_RESPONSE, GET_INSTANCE_TYPE_LIST_RESPONSE, GET_SUPPORTED_REGION_DATA_RESPONSE,
        LIST_ACCESS_KEYS_RESPONSE } from '../actions/cloud';

import { setInitialState, setSuccessState, setFailureState, setLoadingState, setResponseState }  from './common';
import _ from 'lodash';

const INITIAL_STATE = {
  regions: setInitialState([]),
  providers: setInitialState([]),
  instanceTypes: setInitialState([]),
  supportedRegionList: setInitialState([]),
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
      return {...setLoadingState(state, "providers", []), fetchMetadata: false};
    case GET_PROVIDER_LIST_RESPONSE:
      if (action.payload.status !== 200) {
        return {...setFailureState(state, "providers", action.payload.data.error), fetchMetadata: false};
      } else {
        return {...setSuccessState(state, "providers", action.payload.data), fetchMetadata: false};
      }

    case GET_REGION_LIST:
      return setLoadingState(state, "regions", []);
    case GET_REGION_LIST_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "regions", action.payload.data.error);
      } else {
        return setSuccessState(state, "regions", _.sortBy(action.payload.data, "name"));
      }

    case GET_INSTANCE_TYPE_LIST:
      return setLoadingState(state, "instanceTypes", []);
    case GET_INSTANCE_TYPE_LIST_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "instanceTypes", action.payload.data.error);
      } else {
        return setSuccessState(state, "instanceTypes", _.sortBy(action.payload.data, "name"));
      }

    case RESET_PROVIDER_LIST:
      return { ...state, providers: setInitialState([]), regions: setInitialState([]), instanceTypes:setInitialState([])}

    case GET_SUPPORTED_REGION_DATA:
      return setLoadingState(state, "supportedRegionList", []);
    case GET_SUPPORTED_REGION_DATA_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "supportedRegionList", action.payload.data.error);
      } else {
        return setSuccessState(state, "supportedRegionList", _.sortBy(action.payload.data, "name"));
      }

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
    case LIST_ACCESS_KEYS_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "accessKeys", action.payload.data.error);
      } else {
        return setSuccessState(state, "accessKeys", action.payload.data);
      }

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
