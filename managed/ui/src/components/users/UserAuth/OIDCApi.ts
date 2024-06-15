/*
 * Created on Mon Feb 12 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from "axios";
import { ROOT_URL } from "../../../config";


export function getOIDCMappings() {
  const cUUID = localStorage.getItem('customerId');
  return axios.get(`${ROOT_URL}/customers/${cUUID}/oidc_mappings`);
};


export function saveOIDCMappings(mappings: any[]) {
  const cUUID = localStorage.getItem('customerId');
  return axios.put(`${ROOT_URL}/customers/${cUUID}/oidc_mappings`, {
    oidcGroupToYbaRolesPairs: mappings
  });
};

export function deleteOIDCMappingByGroupName(groupName: string) {
  const cUUID = localStorage.getItem('customerId');
  return axios.delete(`${ROOT_URL}/customers/${cUUID}/oidc_mappings/${groupName}`);
};
