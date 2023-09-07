/*
 * Created on Wed Jul 12 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Permission } from '../permission';

export const RoleType = {
  SYSTEM: 'System',
  CUSTOM: 'Custom'
} as const;

export interface Role {
  roleUUID: string;
  name: string;
  description: string;
  customerUUID: string;
  createdOn: string;
  updatedOn: string;
  roleType: typeof RoleType[keyof typeof RoleType];
  permissionDetails: {
    permissionList: Permission[];
  };
}
