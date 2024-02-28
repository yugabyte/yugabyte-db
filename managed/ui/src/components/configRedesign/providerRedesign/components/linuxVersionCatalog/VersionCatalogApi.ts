/*
 * Created on Wed Dec 06 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../../../config';

export const upgradeVM = (universeUUID: string, payload: Record<string, any>) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/upgrade/vm`;
  return axios.post(requestUrl, {
    ...payload
  });
};
