// Copyright (c) YugaByte, Inc.

import { GET_REGION_LIST, GET_REGION_LIST_RESPONSE, GET_PROVIDER_LIST, GET_PROVIDER_LIST_RESPONSE,
  GET_INSTANCE_TYPE_LIST, GET_INSTANCE_TYPE_LIST_RESPONSE, RESET_PROVIDER_LIST,
  GET_SUPPORTED_REGION_DATA, GET_SUPPORTED_REGION_DATA_RESPONSE, CREATE_PROVIDER,
  CREATE_PROVIDER_RESPONSE, CREATE_REGION, CREATE_REGION_RESPONSE, CREATE_ACCESS_KEY,
  CREATE_ACCESS_KEY_RESPONSE, INITIALIZE_PROVIDER, INITIALIZE_PROVIDER_SUCCESS,
  INITIALIZE_PROVIDER_FAILURE, DELETE_PROVIDER, DELETE_PROVIDER_SUCCESS, DELETE_PROVIDER_FAILURE,
  DELETE_PROVIDER_RESPONSE, RESET_PROVIDER_BOOTSTRAP, LIST_ACCESS_KEYS, LIST_ACCESS_KEYS_RESPONSE,
  GET_EBS_TYPE_LIST, GET_EBS_TYPE_LIST_RESPONSE, CREATE_DOCKER_PROVIDER, CREATE_DOCKER_PROVIDER_RESPONSE,
  CREATE_INSTANCE_TYPE, CREATE_INSTANCE_TYPE_RESPONSE, FETCH_CLOUD_METADATA, CREATE_ZONE,
  CREATE_ZONE_RESPONSE, CREATE_NODE_INSTANCE, CREATE_NODE_INSTANCE_RESPONSE, SET_ON_PREM_CONFIG_DATA,
  GET_NODE_INSTANCE_LIST, GET_NODE_INSTANCE_LIST_RESPONSE, RESET_ON_PREM_CONFIG_DATA
} from '../actions/cloud';

import { getInitialState, setInitialState, setSuccessState, setFailureState, setLoadingState, setPromiseResponse }
  from '../utils/PromiseUtils';
import _ from 'lodash';

const INITIAL_STATE = {
  regions: getInitialState([]),
  providers: getInitialState([]),
  instanceTypes: getInitialState([]),
  supportedRegionList: getInitialState([]),
  onPremJsonFormData: {},
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
  accessKeys: getInitialState([]),
  bootstrap: getInitialState({}),
  dockerBootstrap: getInitialState({}),
  status : 'init',
  fetchMetadata: false,
  nodeInstanceList: getInitialState([])
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
      return { ...state, providers: getInitialState([]), regions: getInitialState([]), instanceTypes:getInitialState([])}

    case GET_SUPPORTED_REGION_DATA:
      return setLoadingState(state, "supportedRegionList", []);
    case GET_SUPPORTED_REGION_DATA_RESPONSE:
      if (action.payload.status !== 200) {
        return setFailureState(state, "supportedRegionList", action.payload.data.error);
      }
      return setSuccessState(state, "supportedRegionList", _.sortBy(action.payload.data, "name"));

    case CREATE_PROVIDER:
      return setLoadingState(state, "bootstrap", {type: "provider", response: null});
    case CREATE_PROVIDER_RESPONSE:
      if (action.payload.status === 200) {
        return setSuccessState(state, "bootstrap", {type: "provider", response: action.payload.data});
      }
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "provider"});

    case CREATE_INSTANCE_TYPE:
      return setLoadingState(state, "bootstrap", {type: "instanceType", response: null});
    case CREATE_INSTANCE_TYPE_RESPONSE:
      if (action.payload.status === 200) {
        return setSuccessState(state, "bootstrap", {type: "instanceType", response: action.payload.data});
      }
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "instanceType"});

    case CREATE_REGION:
      return setLoadingState(state, "bootstrap", {type: "region", response: null});
    case CREATE_REGION_RESPONSE:
      if (action.payload.status === 200) {
        return setSuccessState(state, "bootstrap", {type: "region", response: action.payload.data});
      }
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "region"});

    case CREATE_ZONE:
      return setLoadingState(state, "bootstrap", {type: "zone", response: null});
    case CREATE_ZONE_RESPONSE:
      if (action.payload.status === 200) {
        return setSuccessState(state, "bootstrap", {type: "zone", response: action.payload.data});
      }
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "zone"});

    case CREATE_NODE_INSTANCE:
      return setLoadingState(state, "bootstrap", {type: "node", response: null});
    case CREATE_NODE_INSTANCE_RESPONSE:
      if (action.payload.status === 200) {
        return setSuccessState(state, "bootstrap", {type: "node", response: action.payload.data});
      }
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "node"});

    case CREATE_ACCESS_KEY:
      return setLoadingState(state, "bootstrap", {type: "accessKey", response: null});
    case CREATE_ACCESS_KEY_RESPONSE:
      if (action.payload.status === 200) {
        return setSuccessState(state, "bootstrap", {type: "accessKey", response: action.payload.data});
      }
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "accessKey"});

    case INITIALIZE_PROVIDER:
      return setLoadingState(state, "bootstrap", {type: "initialize", response: null});
    case INITIALIZE_PROVIDER_SUCCESS:
      return setSuccessState(state, "bootstrap", {type: "initialize", response: action.payload.data});
    case INITIALIZE_PROVIDER_FAILURE:
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "initialize"});

    case DELETE_PROVIDER:
      return setLoadingState(state, "bootstrap", {type: "cleanup", response: null});
    case DELETE_PROVIDER_SUCCESS:
      return setSuccessState(state, "bootstrap", {type: "cleanup", response: action.payload.data});
    case DELETE_PROVIDER_FAILURE:
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "cleanup"});
    case DELETE_PROVIDER_RESPONSE:
      if (action.payload.status === 200) {
        return setSuccessState(state, "bootstrap", {type: "cleanup", response: action.payload.data});
      }
      return setFailureState(state, "bootstrap", action.payload.data.error, {type: "cleanup"});

    case RESET_PROVIDER_BOOTSTRAP:
      return setInitialState(state, "bootstrap");

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
    case RESET_ON_PREM_CONFIG_DATA:
      return {...state, onPremJsonFormData: {}};

    case GET_NODE_INSTANCE_LIST:
      return setLoadingState(state, "nodeInstanceList", []);
    case GET_NODE_INSTANCE_LIST_RESPONSE:
      return setPromiseResponse(state, "nodeInstanceList", action);
    default:
      return state;
  }
}
