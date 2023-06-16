/*
 * Created on Mon Jun 05 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../config';
import { CACertUploadParams, CACert } from './ICerts';

export const getCACertsList = () => {
  const cUUID = localStorage.getItem('customerId');
  return axios.get<CACert[]>(`${ROOT_URL}/customers/${cUUID}/customCAStoreCertificates`);
};

export const uploadCA = (cert: CACertUploadParams) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/customCAStore`, { ...cert });
};

export const updateCA = (certUUID: string, cert: CACertUploadParams) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/customCAStore/${certUUID}`, { ...cert });
};

export const getCertDetails = (certUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.get<CACert>(`${ROOT_URL}/customers/${cUUID}/customCAStoreCertificates/${certUUID}`);
};

export const deleteCertificate = (certUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  return axios.delete(`${ROOT_URL}/customers/${cUUID}/customCAStoreCertificates/${certUUID}`);
};
