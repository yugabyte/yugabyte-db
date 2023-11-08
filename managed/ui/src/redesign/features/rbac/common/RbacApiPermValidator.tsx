/*
 * Created on Thu Oct 26 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { CSSProperties, FC } from 'react';
import { endsWith, find } from 'lodash';
import { ApiEndpointPermMappingType, ApiProps, Operators } from '../ApiAndUserPermMapping';
import { Permission, Resource, ResourceType } from '../permission';
import { isRbacEnabled } from './RbacUtils';
import { ControlComp, getErrorBoundary } from './validator/ValidatorUtils';
import { UserPermission } from './rbac_constants';

type OnResourceType =
  | 'CUSTOMER_ID'
  | (string & {})
  | Partial<Record<ResourceType, string>>
  | undefined
  | null;

/**
 * Logs rbacs logs
 */
class RBACLogger {
  enabled = false;
  log(message: any) {
    if (!this.enabled) return;
    (console as any).log(message);
  }
}

const rbacLog = new RBACLogger();

export interface RbacApiPermValidatorProps {
  accessRequiredOn?: ApiProps & { onResource?: OnResourceType };
  children: React.ReactNode;
  isControl?: boolean;
  overrideStyle?: CSSProperties;
  customValidateFunction?: (permissions: UserPermission[]) => boolean;
  popOverOverrides?: CSSProperties;
  verbose?: boolean;
}

type RequireProperty<T, Prop extends keyof T> = T & { [key in Prop]-?: T[key] };
type RequireAccessReqOrCustomValidateFn =
  | RequireProperty<RbacApiPermValidatorProps, 'accessRequiredOn'>
  | RequireProperty<RbacApiPermValidatorProps, 'customValidateFunction'>;

const executeOperation = <T,>(operator: keyof typeof Operators, arr: T[]) => {
  switch (operator) {
    case Operators.AND:
      return arr.every(Boolean);
    case Operators.OR:
      return arr.some(Boolean);
    case Operators.NOT:
      return arr.length === 0;
    default:
      throw new Error('Operator not supported');
  }
};

const getResourceId = (permissionDef: Permission, onResource: OnResourceType) => {
  if (!permissionDef.permissionValidOnResource) return undefined;

  let uuid = undefined;
  switch (permissionDef.resourceType) {
    case Resource.UNIVERSE:
      uuid = typeof onResource === 'string' ? onResource : onResource?.UNIVERSE;
      break;
    case Resource.DEFAULT:
      uuid =
        onResource === 'CUSTOMER_ID'
          ? localStorage.getItem('customerId')
          : typeof onResource === 'string'
          ? onResource
          : onResource?.OTHER;
      break;
    case Resource.ROLE:
      uuid = typeof onResource === 'string' ? onResource : onResource?.ROLE;
      break;
    case Resource.USER:
      uuid = typeof onResource === 'string' ? onResource : onResource?.USER;
      break;
    default:
      throw new Error(`resource type ${permissionDef.resourceType} not found`);
  }

  if (!uuid) {
    console.warn(
      `No resource id provided for ${permissionDef.resourceType}.${permissionDef.action}`
    );
  }

  return uuid;
};

export const hasNecessaryPerm = (
  accessRequiredOn: RbacApiPermValidatorProps['accessRequiredOn']
) => {
  if (!isRbacEnabled()) {
    return true;
  }

  if (!accessRequiredOn) throw new Error('AccessRequired on is not Present');

  const { requestType, endpoint, onResource } = accessRequiredOn;

  const api = ((window as any).api_perm_map as ApiEndpointPermMappingType).find(
    (resp) => resp.requestType === requestType && endsWith(resp.endpoint, endpoint)
  );

  if (!api) {
    console.warn('Unable to find Api ', accessRequiredOn);
    return true;
  }

  rbacLog.log({ api });

  const userPermissions: UserPermission[] = (window as any).rbac_permissions;
  const permissionDefinitions: Permission[] = (window as any).all_permissions;

  const listOfAllReqResources = api.rbacPermissionDefinitions.rbacPermissionDefinitionList.map(
    (reqPermissions) => {
      const resourceList = reqPermissions.rbacPermissionList.map((reqPerm) => {
        const permissionDef = find(permissionDefinitions, {
          action: reqPerm.action,
          resourceType: reqPerm.resourceType
        });

        if (!permissionDef) {
          throw new Error(
            `Permission def for action ${reqPerm.action} and resource ${reqPerm.resourceType} not found`
          );
        }

        rbacLog.log({ permissionDef });

        const requiredResource = getResourceId(permissionDef, onResource);

        rbacLog.log({ requiredResource });

        const resource = find(userPermissions, function (p) {
          if (
            reqPerm.resourceType === p.resourceType &&
            p.actions.includes(reqPerm.action) &&
            p.resourceUUID === (requiredResource ? requiredResource : undefined)
          ) {
            return true;
          }
          return false;
        });

        rbacLog.log({ resource });

        return resource;
      });
      return executeOperation(reqPermissions.operator, resourceList);
    }
  );

  return executeOperation(api.rbacPermissionDefinitions.operator, listOfAllReqResources);
};

export const RbacValidator: FC<RequireAccessReqOrCustomValidateFn> = ({
  accessRequiredOn,
  children,
  customValidateFunction,
  isControl,
  overrideStyle,
  popOverOverrides,
  verbose = false
}) => {
  if (!isRbacEnabled()) {
    return <>{children}</>;
  }

  rbacLog.enabled = verbose;

  if (customValidateFunction) {
    rbacLog.log('Entering CustomValidateFunction');

    if (customValidateFunction((window as any).rbac_permissions)) {
      rbacLog.log('CustomValidateFunction Success');

      return <>{children}</>;
    } else {
      rbacLog.log('CustomValidateFunction failure');

      return isControl
        ? ControlComp({ children, overrideStyle, popOverOverrides })
        : getErrorBoundary({ overrideStyle, children });
    }
  }

  const allReqPermSatisfied = hasNecessaryPerm(accessRequiredOn);

  rbacLog.log({ allReqPermSatisfied });

  if (allReqPermSatisfied) {
    return <>{children}</>;
  }

  if (isControl) {
    return ControlComp({ children, overrideStyle, popOverOverrides });
  }

  return getErrorBoundary({ overrideStyle, children });
};

export const customPermValidateFunction = (
  validateFn: RbacApiPermValidatorProps['customValidateFunction']
) => {
  return validateFn?.((window as any).rbac_permissions as UserPermission[]) ?? false;
};
