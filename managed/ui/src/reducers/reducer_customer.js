// Copyright (c) YugaByte, Inc.

import { VALIDATE_FROM_TOKEN, VALIDATE_FROM_TOKEN_RESPONSE,
         REGISTER, REGISTER_RESPONSE, LOGIN, LOGIN_RESPONSE, RESET_CUSTOMER,
         LOGOUT, LOGOUT_SUCCESS, LOGOUT_FAILURE, FETCH_SOFTWARE_VERSIONS_FAILURE, FETCH_SOFTWARE_VERSIONS_SUCCESS,
         FETCH_SOFTWARE_VERSIONS, FETCH_HOST_INFO, FETCH_HOST_INFO_SUCCESS, FETCH_HOST_INFO_FAILURE,
         FETCH_CUSTOMER_COUNT, FETCH_YUGAWARE_VERSION, FETCH_YUGAWARE_VERSION_RESPONSE,
         UPDATE_PROFILE, UPDATE_PROFILE_SUCCESS, UPDATE_PROFILE_FAILURE, ADD_CUSTOMER_CONFIG,
         ADD_CUSTOMER_CONFIG_RESPONSE, FETCH_CUSTOMER_CONFIGS, FETCH_CUSTOMER_CONFIGS_RESPONSE,
         DELETE_CUSTOMER_CONFIG, DELETE_CUSTOMER_CONFIG_RESPONSE } from '../actions/customers';
import {sortVersionStrings} from '../utils/ObjectUtils';
import { getInitialState, setLoadingState, setSuccessState, setFailureState, setPromiseResponse }  from '../utils/PromiseUtils';

const INITIAL_STATE = {
  currentCustomer: getInitialState({}),
  authToken: getInitialState({}),
  tasks: [],
  status: null,
  error: null,
  loading: false,
  softwareVersions: [],
  hostInfo: null,
  customerCount: {},
  yugawareVersion: getInitialState({}),
  profile: getInitialState({}),
  addConfig: getInitialState({}),
  configs: getInitialState([]),
  deleteConfig: getInitialState({})
};

export default function(state = INITIAL_STATE, action) {
  switch(action.type) {
    case VALIDATE_FROM_TOKEN:
      return setLoadingState(state, "currentCustomer", {});
    case VALIDATE_FROM_TOKEN_RESPONSE:
      return setPromiseResponse(state, "currentCustomer", action);

    case REGISTER:
      return setLoadingState(state, "authToken", {});
    case REGISTER_RESPONSE:
      return setPromiseResponse(state, "authToken", action);

    case LOGIN:
      return setLoadingState(state, "authToken", {});
    case LOGIN_RESPONSE:
      return setPromiseResponse(state, "authToken", action);

    case LOGOUT:
      return {...state};
    case LOGOUT_SUCCESS:
      return {...state, currentCustomer: getInitialState({}), authToken: getInitialState({})};
    case LOGOUT_FAILURE:
      return {...state};
    case RESET_CUSTOMER:
      return {...state, currentCustomer: getInitialState({}), authToken: getInitialState({})};
    case FETCH_SOFTWARE_VERSIONS:
      return {...state, softwareVersions: []};
    case FETCH_SOFTWARE_VERSIONS_SUCCESS:
      return {...state, softwareVersions: sortVersionStrings(action.payload.data)};
    case FETCH_SOFTWARE_VERSIONS_FAILURE:
      return {...state};
    case FETCH_HOST_INFO:
      return {...state, hostInfo: null};
    case FETCH_HOST_INFO_SUCCESS:
      return {...state, hostInfo: action.payload.data};
    case FETCH_HOST_INFO_FAILURE:
      return {...state, hostInfo: null };

    case UPDATE_PROFILE:
      return setLoadingState(state, "profile");
    case UPDATE_PROFILE_SUCCESS:
      return setSuccessState(state, "profile", "updated-success");
    case UPDATE_PROFILE_FAILURE:
      return setFailureState(state, "profile", action.payload.data.error);
    case FETCH_CUSTOMER_COUNT:
      return setLoadingState(state, "customerCount");
    case FETCH_YUGAWARE_VERSION:
      return setLoadingState(state, "yugawareVersion", {});
    case FETCH_YUGAWARE_VERSION_RESPONSE:
      return setPromiseResponse(state, "yugawareVersion", action);
    case ADD_CUSTOMER_CONFIG:
      return setLoadingState(state, "addConfig", {});
    case ADD_CUSTOMER_CONFIG_RESPONSE:
      return setPromiseResponse(state, "addConfig", action);
    case FETCH_CUSTOMER_CONFIGS:
      return setLoadingState(state, "configs", []);
    case FETCH_CUSTOMER_CONFIGS_RESPONSE:
      return setPromiseResponse(state, "configs", action);
    case DELETE_CUSTOMER_CONFIG:
      return setLoadingState(state, "deleteConfig", {});
    case DELETE_CUSTOMER_CONFIG_RESPONSE:
      return setPromiseResponse(state, "deleteConfig", action);
    default:
      return state;
  }
}
