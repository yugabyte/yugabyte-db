/*
 * Created on Wed Jul 12 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

export const Action = {
  CREATE: 'CREATE',
  READ: 'READ',
  UPDATE: 'UPDATE',
  DELETE: 'DELETE',
  PAUSE_RESUME: 'PAUSE_RESUME',
  BACKUP_RESTORE: 'BACKUP_RESTORE',
  UPDATE_ROLE_BINDINGS: 'UPDATE_ROLE_BINDINGS',
  UPDATE_PROFILE: 'UPDATE_PROFILE',
  SUPER_ADMIN_ACTIONS: 'SUPER_ADMIN_ACTIONS'
} as const;

export const Resource = {
  UNIVERSE: 'UNIVERSE',
  DEFAULT: 'OTHER',
  ROLE: 'ROLE',
  USER: 'USER'
} as const;

export type ResourceType = typeof Resource[keyof typeof Resource];
export type ActionType = typeof Action[keyof typeof Action];

export interface Permission {
  name: string;
  description: string;
  action: ActionType;
  resourceType: ResourceType;
  permissionValidOnResource: boolean;
  prerequisitePermissions: Pick<Permission, 'action' | 'resourceType'>[];
}
