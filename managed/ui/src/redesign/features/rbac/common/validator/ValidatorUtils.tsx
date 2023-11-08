/*
 * Created on Fri Oct 27 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Typography } from '@material-ui/core';
import { ErrorBoundary } from './ErrorBoundary';
import { CSSProperties } from 'react';
import { ReactComponent as LockIcon } from '../../../../assets/lock.svg';
import { ButtonDisabledPopover } from './ButtonDisabledPopover';

export const RBAC_ERR_MSG_NO_PERM = (
  <Typography
    variant="body2"
    style={{
      fontSize: '13px',
      fontWeight: 400,
      lineHeight: 1.25,
    }}
  >
    You don&apos;t have permission to do this action.
    <br />
    <br />
    If you think you should be able to do this action, contact your administrator.
  </Typography>
);

export const getWrappedChildren = ({ overrideStyle }: { overrideStyle: CSSProperties }) => {
  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        textAlign: 'center',
        ...overrideStyle
      }}
      data-testid="rbac-perm-error"
      id="rbac-perm-error"
    >
      <LockIcon />
      {RBAC_ERR_MSG_NO_PERM}
    </div>
  );
};

export const getErrorBoundary = ({
  overrideStyle = {},
  children
}: {
  overrideStyle?: CSSProperties;
  children: React.ReactNode;
}) => (
  <ErrorBoundary>
    <div
      style={{
        position: 'relative'
      }}
      data-testid="rbac-perm-error"
      id="rbac-perm-error"
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
        {getWrappedChildren({ overrideStyle })}
      </div>
      {children}
    </div>
  </ErrorBoundary>
);

export const ControlComp = ({
  overrideStyle = {},
  children,
  popOverOverrides
}: {
  overrideStyle?: CSSProperties;
  children: React.ReactNode;
  popOverOverrides?: CSSProperties;
}) => (
  <ErrorBoundary>
    <div
      style={{
        opacity: 0.5,
        userSelect: 'none',
        display: 'inline-block',
        ...overrideStyle
      }}
      data-testid="rbac-perm-error"
      id="rbac-perm-error"
    >
      <ButtonDisabledPopover popOverOverrides={popOverOverrides}>
        {children as any}
      </ButtonDisabledPopover>
    </div>
  </ErrorBoundary>
);
