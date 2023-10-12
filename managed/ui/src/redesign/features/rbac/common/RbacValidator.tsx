/*
 * Created on Thu Aug 10 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Component, ErrorInfo, FC, cloneElement, useState } from 'react';
import { find } from 'lodash';
import { Popover, Tooltip, Typography } from '@material-ui/core';
import { CSSProperties } from '@material-ui/styles';
import { ActionType, Permission } from '../permission';
import { isRbacEnabled } from './RbacUtils';
import { RbacResourceTypes, UserPermission } from './rbac_constants';
import { ReactComponent as LockIcon } from '../../../assets/lock.svg';

export interface RbacValidatorProps {
  accessRequiredOn: {
    permissionRequired: readonly ActionType[];
    resourceType: typeof RbacResourceTypes[keyof typeof RbacResourceTypes];
    onResource: 'CUSTOMER_ID' | (string & {}) | undefined | null;
  };
  children: React.ReactNode;
  minimal?: boolean;
  isControl?: boolean;
  onEnd?: (resource: any) => void;
  overrideStyle?: CSSProperties;
  customValidateFunction?: (permissions: UserPermission[]) => boolean;
}

export const RBAC_ERR_MSG_NO_PERM = "You don't have permission";

const findResource = (accessRequiredOn: RbacValidatorProps['accessRequiredOn']) => {
  const { onResource, resourceType } = accessRequiredOn;

  if (!isRbacEnabled()) {
    return null;
  }
  const userPermissions: UserPermission[] = (window as any).rbac_permissions;

  let requiredResource = onResource;

  const allPermissions: Permission[] = (window as any).all_permissions;

  const permissionValidOnResource = accessRequiredOn.permissionRequired.map((permReq) => {
    return find(allPermissions, { action: permReq, resourceType: resourceType });
  });

  if (onResource === 'CUSTOMER_ID') {
    requiredResource = localStorage.getItem('customerId') ?? undefined;
  }

  if (!permissionValidOnResource.some((p) => p?.permissionValidOnResource)) {
    requiredResource = undefined;
  }

  const resource = find(userPermissions, function (perm) {
    if (
      perm.resourceType === resourceType &&
      perm.resourceUUID === (requiredResource ? requiredResource : undefined)
    ) {
      return true;
    }
    return false;
  });
  return resource;
};

export const hasNecessaryPerm = (accessRequiredOn: RbacValidatorProps['accessRequiredOn']) => {
  const { permissionRequired } = accessRequiredOn;

  if (!isRbacEnabled()) {
    return true;
  }

  const resource = findResource(accessRequiredOn);

  if (resource && permissionRequired.every((p) => resource.actions.includes(p))) {
    return true;
  }
  return false;
};

export const RbacValidator: FC<RbacValidatorProps> = ({
  accessRequiredOn,
  children,
  onEnd,
  customValidateFunction,
  minimal = false,
  isControl = false,
  overrideStyle = {}
}) => {
  if (!isRbacEnabled()) {
    return <>{children}</>;
  }

  const { permissionRequired } = accessRequiredOn;

  const resource = findResource(accessRequiredOn);

  if (onEnd) {
    onEnd(resource);
  }

  const controlComp = (
    <ErrorBoundary>
      <div
        style={{
          opacity: 0.5,
          userSelect: 'none',
          display: 'inline-block',
          ...overrideStyle
        }}
        data-testid="rbac-no-perm"
      >
        <ButtonDisabledPopover>{children as any}</ButtonDisabledPopover>
      </div>
    </ErrorBoundary>
  );

  const getWrappedChildren = () => {
    if (minimal) {
      return (
        <Tooltip title="Permission required">
          <LockIcon />
        </Tooltip>
      );
    }
    return (
      <div
        style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}
        data-testid="rbac-no-perm"
      >
        <LockIcon />
        You don&apos;t have permission to view this page
      </div>
    );
  };
  const getErrorBoundary = (
    <ErrorBoundary>
      <div
        style={{
          position: 'relative'
        }}
        data-testid="rbac-no-perm"
      >
        <div
          style={{
            background: '#fffdfdf7',
            position: 'absolute',
            top: 0,
            left: 0,
            width: '100%',
            height: '100%',
            zIndex: 1001,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center'
          }}
        >
          {getWrappedChildren()}
        </div>
        {children}
      </div>
    </ErrorBoundary>
  );
  
  if (customValidateFunction) {
    if (customValidateFunction((window as any).rbac_permissions)) {
      return <>{children}</>;
    } else {
      return isControl ? controlComp : getErrorBoundary;
    }
  } else if (resource && permissionRequired.every((p) => resource.actions.includes(p))) {
    return <>{children}</>;
  }

  if (isControl) {
    return controlComp;
  }

  return getErrorBoundary;
};

type ErrorBoundaryState = {
  hasError: boolean;
};
export class ErrorBoundary extends Component {
  public state: ErrorBoundaryState = {
    hasError: false
  };

  static getDerivedStateFromError(error: Error) {
    return { hasError: true };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    //if error == '401, , then log and display permission needed
    console.error(error, info);
  }

  render() {
    if (this.state.hasError) {
      return <h1>Something went wrong.</h1>;
    }

    return this.props.children;
  }
}

export const ButtonDisabledPopover = ({ children }: { children: React.ReactElement }) => {
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const open = Boolean(anchorEl);
  const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
    setAnchorEl(event.currentTarget);
  };
  const handleClose = () => {
    setAnchorEl(null);
  };

  const reactChild = cloneElement(children, {
    onClick: (e: React.MouseEvent<HTMLButtonElement>) => {
      e.preventDefault();
      e.stopPropagation();
      handleClick(e);
    }
  });

  return (
    <div>
      {reactChild}
      <Popover
        id={'rbac_perm_error'}
        open={open}
        anchorEl={anchorEl}
        onClose={handleClose}
        anchorOrigin={{
          vertical: 'bottom',
          horizontal: 'center'
        }}
        transformOrigin={{
          vertical: 'top',
          horizontal: 'center'
        }}
      >
        <div
          style={{
            padding: '10px',
            width: '350px',
            zIndex: 1001
          }}
        >
          <Typography variant="body2">
            You don’t have permission to do this action.
            <br />
            <br />
            If you think you should be able to do this action, contact your administrator.
          </Typography>
        </div>
      </Popover>
    </div>
  );
};
