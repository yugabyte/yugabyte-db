// Copyright (c) YugabyteDB, Inc.

import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const NewReleaseList = lazy(() =>
  import('../redesign/features/releases/components/ReleaseList').then(({ NewReleaseList }) => ({
    default: NewReleaseList
  }))
);
class Releases extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <Suspense fallback={YBLoadingCircleIcon}>
          <NewReleaseList />
        </Suspense>
      </div>
    );
  }
}

export default Releases;
