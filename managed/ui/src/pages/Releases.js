// Copyright (c) YugaByte, Inc.

import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const ReleaseFlowDecision = lazy(() =>
  import('../components/releases/ReleaseFlowDecision').then(({ ReleaseFlowDecision }) => ({
    default: ReleaseFlowDecision
  }))
);

class Releases extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <Suspense fallback={YBLoadingCircleIcon}>
          <ReleaseFlowDecision />
        </Suspense>
      </div>
    );
  }
}

export default Releases;
