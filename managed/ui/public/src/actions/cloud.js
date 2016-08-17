
import axios from 'axios';

//Get Region List
export const GET_REGION_LIST = 'GET_REGION_LIST';
export const GET_REGION_LIST_SUCCESS = 'GET_REGION_LIST_SUCCESS';
export const GET_REGION_LIST_FAILURE = 'GET_REGION_LIST_FAILURE';

//Get Provider List
export const GET_PROVIDER_LIST = 'GET_PROVIDER_LIST';
export const GET_PROVIDER_LIST_SUCCESS = 'GET_PROVIDER_LIST_SUCCESS';
export const GET_PROVIDER_LIST_FAILURE = 'GET_PROVIDER_LIST_FAILURE';

const ROOT_URL = location.href.indexOf('localhost') > 0 ? 'http://localhost:9000/api' : '/api';

export function getRegionList() {
  const request = axios.get(`${ROOT_URL}/regions`);
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
