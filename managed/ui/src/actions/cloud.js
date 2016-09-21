// Copyright (c) YugaByte, Inc.

import axios from 'axios';
import { ROOT_URL } from '../config';

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

export function getProviderList() {
  var auth_token = localStorage.getItem("customer_token").toString();
  axios.defaults.headers.common['X-AUTH-TOKEN'] = auth_token;
  const request = axios.get(`${ROOT_URL}/providers`);
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

export function getRegionList(provider,isMultiAz) {
  const request = axios.get(`${ROOT_URL}/providers/${provider}/regions?multiAZ=${isMultiAz}`);
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

export function getInstanceTypeList(provider) {
  const request = axios.get(`${ROOT_URL}/providers/${provider}/instance_types`);
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
