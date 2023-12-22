/*
 * Created on May 25 2023
 *
 * Copyright 2023 YugaByte, Inc. and Contributors
 */

import { lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const NodeAgentConfigComponent = lazy(() =>
  import('../redesign/features/NodeAgent/NodeAgentConfig').then(({ NodeAgentConfig }) => ({
    default: NodeAgentConfig
  }))
);

export const NodeAgent = () => {
  return (
    <Suspense fallback={YBLoadingCircleIcon}>
      <NodeAgentConfigComponent />
    </Suspense>
  );
};
