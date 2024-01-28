/*
 * Created on January 14th 2024
 *
 * Copyright 2023 YugaByte, Inc. and Contributors
 */

import { lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const ReleaseListComponent = lazy(() =>
  import('../redesign/features/releases/components/ReleaseList').then(({ ReleaseList }) => ({
    default: ReleaseList
  }))
);

export const ReleaseList = () => {
  return (
    <Suspense fallback={YBLoadingCircleIcon}>
      <ReleaseListComponent />
    </Suspense>
  );
};
