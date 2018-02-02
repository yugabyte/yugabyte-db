// Copyright (c) YugaByte, Inc.

import axios from 'axios';

import {getCustomerEndpoint} from './common';

import { ROOT_URL } from '../config';

export const STOP_NODE = 'STOP_NODE';
export const STOP_NODE_RESPONSE = 'STOP_NODE_RESPONSE';

export const START_NODE = 'START_NODE';
export const START_NODE_RESPONSE = 'START_NODE_RESPONSE';

export const DELETE_NODE = 'DELETE_NODE';
export const DELETE_NODE_RESPONSE = 'DELETE_NODE_RESPONSE';

function nodeActionURI(nodeName, universeUUID) {
  return `${getCustomerEndpoint()}/universes/${universeUUID}/nodes/${nodeName}`;
}
export function stopNode(nodeName, universeUUID) {
  const request = axios.get(`${nodeActionURI(nodeName, universeUUID)}/stop`);
  return {
    type: STOP_NODE,
    payload: request
  };
}

export function stopNodeResponse(response) {
  return {
    type: STOP_NODE_RESPONSE,
    payload: response
  };
}

export function startNode(nodeName, universeUUID) {
  const request = axios.get(`${nodeActionURI(nodeName, universeUUID)}/start`);
  return {
    type: START_NODE,
    payload: request
  };
}

export function startNodeResponse(response) {
  return {
    type: START_NODE_RESPONSE,
    payload: response
  };
}

export function deleteNode(nodeName, universeUUID) {
  const cUUID = localStorage.getItem("customer_id");
  const request = axios.delete(`${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/nodes/${nodeName}`);
  return {
    type: DELETE_NODE,
    payload: request
  };
}

export function deleteNodeResponse(response) {
  return {
    type: DELETE_NODE_RESPONSE,
    payload: response
  };
}
