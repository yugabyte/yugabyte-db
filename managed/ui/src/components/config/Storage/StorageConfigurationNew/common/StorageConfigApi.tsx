/*
 * Created on Thu Jul 28 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../../../config';
import { IStorageProviders } from '../IStorageConfigs';

export const addCustomerConfig = (config: Record<string, any>) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/configs`, config);
};

export const editCustomerConfig = (config: Record<string, any>) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.put(`${ROOT_URL}/customers/${cUUID}/configs/${config.configUUID}`, config);
};

export const fetchBucketsList = (cloud: IStorageProviders, config: Record<string, any>) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/cloud/${cloud.toUpperCase()}/buckets`, config);
};

export const deleteCustomerConfig = (configUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.delete(`${ROOT_URL}/customers/${cUUID}/configs/${configUUID}`);
};
