// Copyright (c) YugaByte, Inc.

import { GET_REGION_LIST, GET_REGION_LIST_RESPONSE, GET_PROVIDER_LIST, GET_PROVIDER_LIST_RESPONSE,
  GET_INSTANCE_TYPE_LIST, GET_INSTANCE_TYPE_LIST_RESPONSE, RESET_PROVIDER_LIST,
  GET_SUPPORTED_REGION_DATA, GET_SUPPORTED_REGION_DATA_RESPONSE, CREATE_PROVIDER,
  CREATE_PROVIDER_RESPONSE, CREATE_REGION, CREATE_REGION_RESPONSE, CREATE_ACCESS_KEY,
  CREATE_ACCESS_KEY_RESPONSE, INITIALIZE_PROVIDER, INITIALIZE_PROVIDER_SUCCESS,
  INITIALIZE_PROVIDER_FAILURE, DELETE_PROVIDER, DELETE_PROVIDER_SUCCESS, DELETE_PROVIDER_FAILURE,
  RESET_PROVIDER_BOOTSTRAP, LIST_ACCESS_KEYS, LIST_ACCESS_KEYS_RESPONSE, GET_EBS_TYPE_LIST,
  GET_EBS_TYPE_LIST_RESPONSE, CREATE_DOCKER_PROVIDER, CREATE_DOCKER_PROVIDER_RESPONSE,
  CREATE_INSTANCE_TYPE, CREATE_INSTANCE_TYPE_RESPONSE, FETCH_CLOUD_METADATA, CREATE_ZONE,
  CREATE_ZONE_RESPONSE, CREATE_NODE_INSTANCE, CREATE_NODE_INSTANCE_RESPONSE, SET_ON_PREM_CONFIG_DATA
} from '../actions/cloud';

import { setInitialState, setSuccessState, setFailureState, setLoadingState, setPromiseResponse }
  from '../utils/PromiseUtils';
import _ from 'lodash';

const INITIAL_STATE = {
  regions: setInitialState([]),
  providers: setInitialState([]),
  instanceTypes: setInitialState([]),
  supportedRegionList: setInitialState([]),
  onPremJsonFormData: [],
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
  fetchMetadata: false
};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {

    case GET_PROVIDER_LIST:
      return {...setLoadingState(state, "providers", []), fetchMetadata: false};
    case GET_PROVIDER_LIST_RESPONSE:
      if (action.payload.status !== 200) {
        return {...setFailureState(state, "providers", action.payload.data.error), fetchMetadata: false};
      }
      return {...setSuccessState(state, "providers", action.payload.data), fetchMetadata: false};

    case GET_REGION_LIST:
      return setLoadingState(state, "regions", []);
    case GET_REGION_LIST_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "regions", action.payload.data.error);
      }
      return setSuccessState(state, "regions", _.sortBy(action.payload.data, "name"));

    case GET_INSTANCE_TYPE_LIST:
      return setLoadingState(state, "instanceTypes", []);
    case GET_INSTANCE_TYPE_LIST_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "instanceTypes", action.payload.data.error);
      }
      return setSuccessState(state, "instanceTypes", _.sortBy(action.payload.data, "name"));

    case RESET_PROVIDER_LIST:
      return { ...state, providers: setInitialState([]), regions: setInitialState([]), instanceTypes:setInitialState([])};

    case GET_SUPPORTED_REGION_DATA:
      return setLoadingState(state, "supportedRegionList", []);
    case GET_SUPPORTED_REGION_DATA_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "supportedRegionList", action.payload.data.error);
      }
      return setSuccessState(state, "supportedRegionList", _.sortBy(action.payload.data, "name"));

    case CREATE_PROVIDER:
      return { ...state, bootstrap: {type: 'provider', response: null, loading: true}};
    case CREATE_PROVIDER_RESPONSE:
      if (action.payload.status === 200) {
        return { ...state, bootstrap: {type: 'provider', response: action.payload.data, loading: false}};
      }
      return { ...state, bootstrap: {type: 'provider', error: action.payload.data.error, loading: false}};

    case CREATE_INSTANCE_TYPE:
      return { ...state, bootstrap: {type: 'instanceType', response: null, loading: true}};
    case CREATE_INSTANCE_TYPE_RESPONSE:
      if (action.payload.status === 200) {
        return { ...state, bootstrap: {type: 'instanceType', response: action.payload.data, loading: false}};
      }
      return { ...state, bootstrap: {type: 'instanceType', error: action.payload.data.error, loading: false}};

    case CREATE_REGION:
      return { ...state, bootstrap: {type: 'region', response: null, loading: true}};
    case CREATE_REGION_RESPONSE:
      if (action.payload.status === 200) {
        return { ...state, bootstrap: {type: 'region', response: action.payload.data, loading: false}};
      }
      return { ...state, bootstrap: {type: 'region', error: action.payload.data.error, loading: false}};

    case CREATE_ZONE:
      return { ...state, bootstrap: {type: 'zone', response: null, loading: true}};
    case CREATE_ZONE_RESPONSE:
      if (action.payload.status === 200) {
        return { ...state, bootstrap: {type: 'zone', response: action.payload.data, loading: false}};
      }
      return { ...state, bootstrap: {type: 'zone', error: action.payload.data.error, loading: false}};

    case CREATE_NODE_INSTANCE:
      return { ...state, bootstrap: {type: 'node', response: null, loading: true}};
    case CREATE_NODE_INSTANCE_RESPONSE:
      if (action.payload.status === 200) {
        return { ...state, bootstrap: {type: 'node', response: action.payload.data, loading: false}};
      }
      return { ...state, bootstrap: {type: 'node', error: action.payload.data.error, loading: false}};

    case CREATE_ACCESS_KEY:
      return { ...state, bootstrap: {type: 'accessKey', response: null, loading: true}};
    case CREATE_ACCESS_KEY_RESPONSE:
      if (action.payload.status === 200) {
        return { ...state, bootstrap: {type: 'accessKey', response: action.payload.data, loading: false}};
      }
      return { ...state, bootstrap: {type: 'accessKey', error: action.payload.data.error, loading: false}};

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
      return setLoadingState(state, "accessKeys", []);
    case LIST_ACCESS_KEYS_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "accessKeys", action.payload.data.error);
      }
      return setSuccessState(state, "accessKeys", action.payload.data);

    case GET_EBS_TYPE_LIST:
      return {...state, ebsTypes: [], status: 'storage', error: null, loading: _.assign(state.loading, {ebsTypes: true})};
    case GET_EBS_TYPE_LIST_RESPONSE:
      if (action.payload.status === 200)
        return { ...state, ebsTypes: action.payload.data, loading: _.assign(state.loading, {ebsTypes: false})};
      return { ...state, ebsTypes: [], error: error, loading: _.assign(state.loading, {ebsTypes: false})};

    case CREATE_DOCKER_PROVIDER:
      return setLoadingState(state, "dockerBootstrap", {});
    case CREATE_DOCKER_PROVIDER_RESPONSE:
      return setPromiseResponse(state, "dockerBootstrap", action);

    case FETCH_CLOUD_METADATA:
      return {...state, fetchMetadata: true};

    case SET_ON_PREM_CONFIG_DATA:
      return {...state, onPremJsonFormData: action.payload};

    default:
      return state;
  }
}
