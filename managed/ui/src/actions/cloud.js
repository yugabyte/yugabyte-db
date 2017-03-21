// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

import { PROVIDER_TYPES, getProviderEndpoint } from './common';

// Get Region List
export const GET_REGION_LIST = 'GET_REGION_LIST';
export const GET_REGION_LIST_SUCCESS = 'GET_REGION_LIST_SUCCESS';
export const GET_REGION_LIST_FAILURE = 'GET_REGION_LIST_FAILURE';

// Get Provider List
export const GET_PROVIDER_LIST = 'GET_PROVIDER_LIST';
export const GET_PROVIDER_LIST_SUCCESS = 'GET_PROVIDER_LIST_SUCCESS';
export const GET_PROVIDER_LIST_FAILURE = 'GET_PROVIDER_LIST_FAILURE';

// Get Instance Type List
export const GET_INSTANCE_TYPE_LIST = 'GET_INSTANCE_TYPE_LIST';
export const GET_INSTANCE_TYPE_LIST_SUCCESS = 'GET_INSTANCE_TYPE_LIST_SUCCESS';
export const GET_INSTANCE_TYPE_LIST_FAILURE = 'GET_INSTANCE_TYPE_LIST_FAILURE';

export const RESET_PROVIDER_LIST = 'RESET_PROVIDER_LIST';

export const GET_SUPPORTED_REGION_DATA = 'GET_SUPPORTED_REGION_DATA';
export const GET_SUPPORTED_REGION_DATA_SUCCESS = 'GET_SUPPORTED_REGION_DATA_SUCCESS';
export const GET_SUPPORTED_REGION_DATA_FAILURE = 'GET_SUPPORTED_REGION_DATA_FAILURE';

export const CREATE_PROVIDER = 'CREATE_PROVIDER';
export const CREATE_PROVIDER_SUCCESS = 'CREATE_PROVIDER_SUCCESS';
export const CREATE_PROVIDER_FAILURE = 'CREATE_PROVIDER_FAILURE';

export const CREATE_REGION = 'CREATE_REGION';
export const CREATE_REGION_SUCCESS = 'CREATE_REGION_SUCCESS';
export const CREATE_REGION_FAILURE = 'CREATE_REGION_FAILURE';

export const CREATE_ACCESS_KEY = 'CREATE_ACCESS_KEY';
export const CREATE_ACCESS_KEY_SUCCESS = 'CREATE_ACCESS_KEY_SUCCESS';
export const CREATE_ACCESS_KEY_FAILURE = 'CREATE_ACCESS_KEY_FAILURE';

export const INITIALIZE_PROVIDER = 'INITIALIZE_PROVIDER';
export const INITIALIZE_PROVIDER_SUCCESS = 'INITIALIZE_PROVIDER_SUCCESS';
export const INITIALIZE_PROVIDER_FAILURE = 'INITIALIZE_PROVIDER_FAILURE';

export function getProviderList() {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/providers`);
  return {
    type: GET_PROVIDER_LIST,
    payload: request
  }
}

export function getProviderListSuccess(providerList) {

  return {
    type: GET_PROVIDER_LIST_SUCCESS,
    payload: providerList.data
  };
}

export function getProviderListFailure(error) {
  return {
    type: GET_PROVIDER_LIST_FAILURE,
    payload: error
  };
}

export function getRegionList(providerUUID, isMultiAz) {
  var cUUID = localStorage.getItem("customer_id");
  const request =
    axios.get(`${ROOT_URL}/customers/${cUUID}/providers/${providerUUID}/regions?multiAZ=${isMultiAz}`);
  return {
    type: GET_REGION_LIST,
    payload: request
  };
}

export function getRegionListSuccess(regionList) {
  return {
    type: GET_REGION_LIST_SUCCESS,
    payload: regionList
  };
}

export function getRegionListFailure(error) {
  return {
    type: GET_REGION_LIST_FAILURE,
    payload: error
  };
}

export function getInstanceTypeList(providerUUID) {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/providers/${providerUUID}/instance_types`);
  return {
    type: GET_INSTANCE_TYPE_LIST,
    payload: request
  };
}

export function getInstanceTypeListSuccess(instanceTypeList) {
  return {
    type: GET_INSTANCE_TYPE_LIST_SUCCESS,
    payload: instanceTypeList
  };
}

export function getInstanceTypeListFailure(error) {
  return {
    type: GET_INSTANCE_TYPE_LIST_FAILURE,
    payload: error
  };
}

export function getSupportedRegionData() {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/regions`);
  return {
    type: GET_SUPPORTED_REGION_DATA,
    payload: request
  }
}

export function getSupportedRegionDataSuccess(regionData) {
  return {
    type: GET_SUPPORTED_REGION_DATA_SUCCESS,
    payload: regionData
  }
}

export function getSupportedRegionDataFailure(error) {
  return {
    type: GET_SUPPORTED_REGION_DATA_FAILURE,
    payload: error
  }
}

export function resetProviderList() {
  return {
    type: RESET_PROVIDER_LIST
  }
}

export function createProvider(type, config) {
  var customerUUID = localStorage.getItem("customer_id");
  var provider = PROVIDER_TYPES.find( (providerType) => providerType.code === type );
  var formValues = {
    'code': provider.code,
    'name': provider.name,
    'config': config
  }

  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/providers`, formValues);
  return {
    type: CREATE_PROVIDER,
    payload: request
  };
}

export function createProviderSuccess(result) {
  return {
    type: CREATE_PROVIDER_SUCCESS,
    payload: result
  };
}

export function createProviderFailure(error) {
  return {
    type: CREATE_PROVIDER_FAILURE,
    payload: error
  }
}

export function createRegion(providerUUID, regionCode) {
  var formValues = { "code": regionCode }
  var url = getProviderEndpoint(providerUUID) + '/regions';
  const request = axios.post(url, formValues);
  return {
    type: CREATE_REGION,
    payload: request
  };
}

export function createRegionSuccess(result) {
  return {
    type: CREATE_REGION_SUCCESS,
    payload: result
  };
}

export function createRegionFailure(error) {
  return {
    type: CREATE_REGION_FAILURE,
    payload: error
  }
}

export function createAccessKey(providerUUID, regionUUID, keyCode) {
  var formValues = { keyCode: keyCode, regionUUID:  regionUUID}
  var url = getProviderEndpoint(providerUUID) + '/access_keys';
  const request = axios.post(url, formValues);
  return {
    type: CREATE_ACCESS_KEY,
    payload: request
  };
}

export function createAccessKeySuccess(result) {
  return {
    type: CREATE_ACCESS_KEY_SUCCESS,
    payload: result
  };
}

export function createAccessKeyFailure(error) {
  return {
    type: CREATE_ACCESS_KEY_FAILURE,
    payload: error
  }
}

export function initializeProvider(providerUUID) {
  const request = axios.get(`${ROOT_URL}/providers/${providerUUID}/initialize`);
  return {
    type: INITIALIZE_PROVIDER,
    payload: request
  };
}

export function initializeProviderSuccess(result) {
  return {
    type: INITIALIZE_PROVIDER_SUCCESS,
    payload: result
  };
}

export function initializeProviderFailure(error) {
  return {
    type: INITIALIZE_PROVIDER_FAILURE,
    payload: error
  }
}
