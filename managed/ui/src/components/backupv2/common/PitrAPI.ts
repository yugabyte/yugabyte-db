/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../config';

export function getNameSpaces(uUUID: string) {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${uUUID}/namespaces`;
  return axios.get(requestUrl).then((resp) => resp.data);
}

export function getPITRConfigs(uUUID: string) {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${uUUID}/pitr_config`;
  return axios.get(requestUrl).then((resp) => resp.data);
}

export function createPITRConfig(
  uUUID: string,
  tableType: string,
  keyspaceName: string,
  payload: Record<string, string>
) {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${uUUID}/keyspaces/${tableType}/${keyspaceName}/pitr_config`;
  return axios.post(requestUrl, payload);
}

export function deletePITRConfig(uUUID: string, pUUID: string) {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${uUUID}/pitr_config/${pUUID}`;
  return axios.delete(requestUrl).then((resp) => resp.data);
}

export function restoreSnapShot(uUUID: string, payload: Record<string, string>) {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${uUUID}/pitr`;
  return axios.post(requestUrl, payload);
}
