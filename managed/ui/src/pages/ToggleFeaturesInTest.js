// Copyright (c) YugaByte, Inc.

import { lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const ToggleFeatureComponent = lazy(() => import('../components/toggleFeatureInTest'));

const ToggleFeaturesInTest = () => {
  return (
    <Suspense fallback={YBLoadingCircleIcon}>
      <ToggleFeatureComponent />
    </Suspense>
  );
};

export default ToggleFeaturesInTest;
