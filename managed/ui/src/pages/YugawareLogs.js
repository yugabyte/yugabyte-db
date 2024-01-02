// Copyright (c) YugaByte, Inc.
import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const YugawareLogsContainer = lazy(() =>
  import('../components/yugaware_logs/YugawareLogsContainer')
);

class YugawareLogs extends Component {
  render() {
    return (
      <Suspense fallback={YBLoadingCircleIcon}>
        <div className="dashboard-container">
          <YugawareLogsContainer />
        </div>
      </Suspense>
    );
  }
}

export default YugawareLogs;
