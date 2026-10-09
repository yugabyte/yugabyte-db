// Copyright (c) YugabyteDB, Inc.

import { ReactElement } from 'react';
import { mui, YBTooltip } from '@yugabyte-ui-library/core';
import { useK8OperatorEditBlockedTooltip } from './EditUniverseUtils';

const { MenuItem } = mui;

export function K8OperatorEditBlockedTooltip({ children }: { children: ReactElement }) {
  const title = useK8OperatorEditBlockedTooltip();
  if (!title) return children;
  const menuItem = children.type === MenuItem;
  return (
    <YBTooltip title={title}>
      <span
        style={
          menuItem ? { display: 'block', width: '100%' } : { display: 'inline-block' }
        }
      >
        {children}
      </span>
    </YBTooltip>
  );
}
