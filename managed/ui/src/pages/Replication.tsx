/*
 * Created on May 25 2023
 *
 * Copyright 2023 YugaByte, Inc. and Contributors
 */

import { lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const ReplicationComponent = lazy(() =>
  import('../components/xcluster/configDetails/ReplicationDetails').then(
    ({ ReplicationDetails }) => ({
      default: ReplicationDetails
    })
  )
);

export const Replication = (props: any) => {
  return (
    <Suspense fallback={YBLoadingCircleIcon}>
      <ReplicationComponent {...props} />
    </Suspense>
  );
};
