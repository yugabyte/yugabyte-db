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
export const CREATE_PROVIDER_RESPONSE = 'CREATE_PROVIDER_RESPONSE';

export const CREATE_INSTANCE_TYPE = 'CREATE_INSTANCE_TYPE';
export const CREATE_INSTANCE_TYPE_RESPONSE = 'CREATE_INSTANCE_TYPE_RESPONSE';

export const CREATE_REGION = 'CREATE_REGION';
export const CREATE_REGION_RESPONSE = 'CREATE_REGION_RESPONSE';

export const CREATE_ZONE = 'CREATE_AVAILABILITY_ZONE';
export const CREATE_ZONE_RESPONSE = 'CREATE_AVAILABILITY_ZONE_RESPONSE';

export const CREATE_NODE_INSTANCE = 'CREATE_NODE_INSTANCE';
export const CREATE_NODE_INSTANCE_RESPONSE = 'CREATE_NODE_INSTANCE_RESPONSE';

export const CREATE_ACCESS_KEY = 'CREATE_ACCESS_KEY';
export const CREATE_ACCESS_KEY_RESPONSE = 'CREATE_ACCESS_KEY_RESPONSE';

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

export const SET_ON_PREM_CONFIG_DATA = 'SET_ON_PREM_CONFIG_DATA';

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
  const baseUrl = getProviderEndpoint(providerUUID);
  const request = axios.get(`${baseUrl}/regions?multiAZ=${isMultiAz}`);
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
  const url = getProviderEndpoint(providerUUID) + '/instance_types';
  const request = axios.get(url);
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

export function createInstanceType(providerCode, providerUUID, instanceTypeInfo) {
  const formValues = {
    'idKey': {
      'providerCode': providerCode,
      'instanceTypeCode': instanceTypeInfo.instanceTypeCode
    },
    'numCores': instanceTypeInfo.numCores,
    'memSizeGB': instanceTypeInfo.memSizeGB,
    'instanceTypeDetails': {
      'volumeDetailsList': instanceTypeInfo.volumeDetailsList
    }
  };
  const url = getProviderEndpoint(providerUUID) + '/instance_types';
  const request = axios.post(url, formValues);
  return {
    type: CREATE_INSTANCE_TYPE,
    payload: request
  }
}

export function createInstanceTypeResponse(responsePayload) {
  return {
    type: CREATE_INSTANCE_TYPE_RESPONSE,
    payload: responsePayload
  }
}

export function getSupportedRegionData() {
  const cUUID = localStorage.getItem("customer_id");
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
  const customerUUID = localStorage.getItem("customer_id");
  const provider = PROVIDER_TYPES.find( (providerType) => providerType.code === type );
  const formValues = {
    'code': provider.code,
    'name': name,
    'config': config
  };

  const request = axios.post(`${ROOT_URL}/customers/${customerUUID}/providers`, formValues);
  return {
    type: CREATE_PROVIDER,
    payload: request
  };
}

export function createProviderResponse(result) {
  return {
    type: CREATE_PROVIDER_RESPONSE,
    payload: result
  };
}

export function createRegion(providerUUID, regionCode, hostVPCId) {
  const formValues = { "code": regionCode, "hostVPCId": hostVPCId, "name": regionCode };
  const url = getProviderEndpoint(providerUUID) + '/regions';
  const request = axios.post(url, formValues);
  return {
    type: CREATE_REGION,
    payload: request
  };
}

export function createRegionResponse(result) {
  return {
    type: CREATE_REGION_RESPONSE,
    payload: result
  };
}

export function createZone(providerUUID, regionUUID, zoneCode) {
  const formValues = { "code": zoneCode, "name": zoneCode };
  const url = getProviderEndpoint(providerUUID) + '/regions/' + regionUUID + '/zones';
  const request = axios.post(url, formValues);
  return {
    type: CREATE_ZONE,
    payload: request
  }
}

export function createZoneResponse(result) {
  return {
    type: CREATE_ZONE_RESPONSE,
    payload: result
  };
}

export function createNodeInstance(zoneUUID, nodeInfo) {
  const customerUUID = localStorage.getItem("customer_id");
  const request =
    axios.post(`${ROOT_URL}/customers/${customerUUID}/zones/${zoneUUID}/nodes`, nodeInfo);
  return {
    type: CREATE_NODE_INSTANCE,
    payload: request
  }
}

export function createNodeInstanceResponse(result) {
  return {
    type: CREATE_NODE_INSTANCE_RESPONSE,
    payload: result
  }
}

export function createAccessKey(providerUUID, regionUUID, keyInfo) {
  const formValues = {
    keyCode: keyInfo.code,
    regionUUID: regionUUID,
    keyType: "PRIVATE",
    keyContent: keyInfo.privateKeyContent
  };
  const url = getProviderEndpoint(providerUUID) + '/access_keys';
  const request = axios.post(url, formValues);
  return {
    type: CREATE_ACCESS_KEY,
    payload: request
  };
}

export function createAccessKeyResponse(result) {
  return {
    type: CREATE_ACCESS_KEY_RESPONSE,
    payload: result
  };
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
  const url = getProviderEndpoint(providerUUID) + '/access_keys';
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

export function setOnPremConfigData(configData) {
  return {
    type: SET_ON_PREM_CONFIG_DATA,
    payload: configData
  };
}
