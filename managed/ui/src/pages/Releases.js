// Copyright (c) YugaByte, Inc.

import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';
// import { ReleaseListContainer } from '../components/releases';

const ReleaseListContainer = lazy(() =>
  import('../components/releases/ReleaseList/ReleaseListContainer')
);

class Releases extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <Suspense fallback={YBLoadingCircleIcon}>
          <ReleaseListContainer />
        </Suspense>
      </div>
    );
  }
}

export default Releases;
