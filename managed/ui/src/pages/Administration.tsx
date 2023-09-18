/*
 * Created on May 25 2023
 *
 * Copyright 2023 YugaByte, Inc. and Contributors
 */

import { lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const AdministrationComponent = lazy(() =>
  import('../components/administration/Administration').then(({ Administration }) => ({
    default: Administration
  }))
);

export const Administration = (props: any) => {
  return (
    <Suspense fallback={YBLoadingCircleIcon}>
      <AdministrationComponent {...props} />
    </Suspense>
  );
};
