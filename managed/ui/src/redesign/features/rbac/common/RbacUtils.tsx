/*
 * Created on Wed Aug 09 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { find } from 'lodash';
import { makeStyles } from '@material-ui/core';
import { RbacBindings } from '../users/components/UserUtils';
import { Role } from '../roles';
import { Action, ActionType, Resource, ResourceType } from '../permission';

const useStyles = makeStyles((theme) => ({
  roleType: {
    borderRadius: '6px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    padding: '4px 6px',
    '&.custom': {
      border: `1px solid ${theme.palette.primary[300]}`,
      background: theme.palette.primary[200],
      color: theme.palette.primary[600]
    }
  }
}));

export const RoleTypeComp: FC<{ role: Role; customLabel?: string }> = ({ role, customLabel }) => {
  const classes = useStyles();
  return (
    <span
      className={clsx(classes.roleType, role.roleType === 'Custom' && 'custom')}
      data-testid="rbac-role-type"
    >
      {customLabel ?? role.roleType}
    </span>
  );
};

export const RBAC_RUNTIME_FLAG = 'yb.rbac.use_new_authz';

export const rbac_identifier = 'rbac_enabled';

export const setIsRbacEnabled = (flag: boolean) => {
  localStorage.setItem(rbac_identifier, flag + '');
};

export const isRbacEnabled = () => {
  return localStorage.getItem(rbac_identifier) === 'true';
};

export const getRbacEnabledVal = () => {
  return localStorage.getItem(rbac_identifier);
};

export const clearRbacCreds = () => {
  localStorage.removeItem(rbac_identifier);
  delete (window as any).rbac_permissions;
};

export const resourceOrderByRelevance = [
  Resource.UNIVERSE,
  Resource.USER,
  Resource.ROLE,
  Resource.DEFAULT
];

export const RBAC_USER_MNG_ROUTE = '/admin/rbac?tab=users';

export const permissionOrderByRelevance = [
  Action.CREATE,
  Action.READ,
  Action.UPDATE,
  Action.BACKUP_RESTORE,
  Action.PAUSE_RESUME,
  Action.UPDATE_PROFILE,
  Action.UPDATE_ROLE_BINDINGS,
  Action.SUPER_ADMIN_ACTIONS,
  Action.DELETE
];

export const userhavePermInRoleBindings = (resourceType: ResourceType, action: ActionType) => {
  const userRoleBindings: RbacBindings[] = (window as any).user_role_bindings;
  return userRoleBindings.some((roleBinding) => {
    return find(roleBinding.role.permissionDetails.permissionList, { action, resourceType }) !== undefined;
  });
};
