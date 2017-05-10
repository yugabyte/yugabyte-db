// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

import { PROVIDER_TYPES, getProviderEndpoint } from './common';

// Get Region List
export const GET_REGION_LIST = 'GET_REGION_LIST';
export const GET_REGION_LIST_RESPONSE = 'GET_REGION_LIST_RESPONSE';

// Get Provider List
export const GET_PROVIDER_LIST = 'GET_PROVIDER_LIST';
export const GET_PROVIDER_LIST_RESPONSE = 'GET_PROVIDER_LIST_RESPONSE';

// Get Instance Type List
export const GET_INSTANCE_TYPE_LIST = 'GET_INSTANCE_TYPE_LIST';
export const GET_INSTANCE_TYPE_LIST_RESPONSE = 'GET_INSTANCE_TYPE_LIST_RESPONSE';

export const RESET_PROVIDER_LIST = 'RESET_PROVIDER_LIST';

export const GET_SUPPORTED_REGION_DATA = 'GET_SUPPORTED_REGION_DATA';
export const GET_SUPPORTED_REGION_DATA_RESPONSE = 'GET_SUPPORTED_REGION_DATA_RESPONSE';

export const CREATE_PROVIDER = 'CREATE_PROVIDER';
export const CREATE_PROVIDER_SUCCESS = 'CREATE_PROVIDER_SUCCESS';
export const CREATE_PROVIDER_FAILURE = 'CREATE_PROVIDER_FAILURE';

export const CREATE_ONPREM_PROVIDER = 'CREATE_ONPREM_PROVIDER';
export const CREATE_ONPREM_PROVIDER_RESPONSE = 'CREATE_ONPREM_PROVIDER_RESPONSE';

export const CREATE_REGION = 'CREATE_REGION';
export const CREATE_REGION_SUCCESS = 'CREATE_REGION_SUCCESS';
export const CREATE_REGION_FAILURE = 'CREATE_REGION_FAILURE';

export const CREATE_ACCESS_KEY = 'CREATE_ACCESS_KEY';
export const CREATE_ACCESS_KEY_SUCCESS = 'CREATE_ACCESS_KEY_SUCCESS';
export const CREATE_ACCESS_KEY_FAILURE = 'CREATE_ACCESS_KEY_FAILURE';

export const INITIALIZE_PROVIDER = 'INITIALIZE_PROVIDER';
export const INITIALIZE_PROVIDER_SUCCESS = 'INITIALIZE_PROVIDER_SUCCESS';
export const INITIALIZE_PROVIDER_FAILURE = 'INITIALIZE_PROVIDER_FAILURE';

export const DELETE_PROVIDER = 'DELETE_PROVIDER';
export const DELETE_PROVIDER_SUCCESS = 'DELETE_PROVIDER_SUCCESS';
export const DELETE_PROVIDER_FAILURE = 'DELETE_PROVIDER_FAILURE';

export const RESET_PROVIDER_BOOTSTRAP = 'RESET_PROVIDER_BOOTSTRAP';

export const LIST_ACCESS_KEYS = 'LIST_ACCESS_KEYS';
export const LIST_ACCESS_KEYS_RESPONSE = 'LIST_ACCESS_KEYS_RESPONSE';

export const GET_EBS_TYPE_LIST = 'GET_EBS_TYPES';
export const GET_EBS_TYPE_LIST_RESPONSE = 'GET_EBS_TYPES_RESPONSE';

export const CREATE_DOCKER_PROVIDER = 'CREATE_DOCKER_PROVIDER';
export const CREATE_DOCKER_PROVIDER_RESPONSE = 'CREATE_DOCKER_PROVIDER_RESPONSE';

export const FETCH_CLOUD_METADATA = 'FETCH_CLOUD_METADATA';

export function getProviderList() {
  const cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/providers`);
  return {
    type: GET_PROVIDER_LIST,
    payload: request
  }
}

export function getProviderListResponse(responsePayload) {
  return {
    type: GET_PROVIDER_LIST_RESPONSE,
    payload: responsePayload
  }
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

export function getRegionListResponse(responsePayload) {
  return {
    type: GET_REGION_LIST_RESPONSE,
    payload: responsePayload
  }
}

export function getInstanceTypeList(providerUUID) {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/providers/${providerUUID}/instance_types`);
  return {
    type: GET_INSTANCE_TYPE_LIST,
    payload: request
  };
}

export function getInstanceTypeListResponse(responsePayload) {
  return {
    type: GET_INSTANCE_TYPE_LIST_RESPONSE,
    payload: responsePayload
  }
}

export function getSupportedRegionData() {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.get(`${ROOT_URL}/customers/${cUUID}/regions`);
  return {
    type: GET_SUPPORTED_REGION_DATA,
    payload: request
  }
}

export function getSupportedRegionDataResponse(responsePayload) {
  return {
    type: GET_SUPPORTED_REGION_DATA_RESPONSE,
    payload: responsePayload
  }
}

export function resetProviderList() {
  return {
    type: RESET_PROVIDER_LIST
  }
}

export function createProvider(type, name, config) {
  var customerUUID = localStorage.getItem("customer_id");
  var provider = PROVIDER_TYPES.find( (providerType) => providerType.code === type );
  var formValues = {
    'code': provider.code,
    'name': name,
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

export function createOnPremProvider(config) {
  var customerUUID = localStorage.getItem("customer_id");
  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/providers/onprem`, config);
  return {
    type: CREATE_ONPREM_PROVIDER,
    payload: request
  };
}

export function createOnPremProviderResponse(result) {
  return {
    type: CREATE_ONPREM_PROVIDER_RESPONSE,
    payload: result
  };
}

export function createRegion(providerUUID, regionCode, hostVPCId) {
  var formValues = { "code": regionCode, "hostVPCId": hostVPCId };
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
  var url = getProviderEndpoint(providerUUID) + '/initialize';
  const request = axios.get(url);
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

export function deleteProvider(providerUUID) {
  const cUUID = localStorage.getItem("customer_id");
  const request =
    axios.delete(`${ROOT_URL}/customers/${cUUID}/providers/${providerUUID}`);
  return {
    type: DELETE_PROVIDER,
    payload: request
  };
}

export function deleteProviderSuccess(data) {
  return {
    type: DELETE_PROVIDER_SUCCESS,
    payload: data
  };
}

export function deleteProviderFailure(error) {
  return {
    type: DELETE_PROVIDER_FAILURE,
    payload: error
  };
}

export function resetProviderBootstrap() {
  return {
    type: RESET_PROVIDER_BOOTSTRAP
  }
}

export function listAccessKeys(providerUUID) {
  var url = getProviderEndpoint(providerUUID) + '/access_keys';
  const request = axios.get(url);
  return {
    type: LIST_ACCESS_KEYS,
    payload: request
  };
}

export function listAccessKeysResponse(response) {
  return {
    type: LIST_ACCESS_KEYS_RESPONSE,
    payload: response
  }
}

export function getEBSTypeList() {
  const request = axios.get(`${ROOT_URL}/metadata/ebs_types`);
  return {
    type: GET_EBS_TYPE_LIST,
    payload: request
  }
}

export function getEBSTypeListResponse(responsePayload) {
  return {
    type: GET_EBS_TYPE_LIST_RESPONSE,
    payload: responsePayload
  }
}

export function createDockerProvider() {
  var cUUID = localStorage.getItem("customer_id");
  const request = axios.post(`${ROOT_URL}/customers/${cUUID}/providers/setup_docker`);
  return {
    type: CREATE_DOCKER_PROVIDER,
    payload: request
  }
}

export function createDockerProviderResponse(response) {
  return {
    type: CREATE_DOCKER_PROVIDER_RESPONSE,
    payload: response
  }
}

export function fetchCloudMetadata() {
  return {
    type: FETCH_CLOUD_METADATA
  }
}
