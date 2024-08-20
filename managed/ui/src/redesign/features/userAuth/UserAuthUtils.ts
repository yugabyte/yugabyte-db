/*
 * Created on Thu Aug 01 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { trimEnd, trimStart } from 'lodash';

export const TOAST_OPTIONS = { autoClose: 1750 };

export const UserDefaultRoleOptions = {
  ReadOnly: 'ReadOnly',
  ConnectOnly: 'ConnectOnly'
};

export const escapeStr = (str: string) => {
  let s = trimStart(str, '""');
  s = trimEnd(s, '""');
  return s;
};
