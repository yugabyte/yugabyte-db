// Copyright (c) YugaByte, Inc.

import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const AlertsListContainer = lazy(() =>
  import('../components/alerts/AlertList/AlertsListContainer')
);

export default class Alerts extends Component {
  render() {
    return (
      <Suspense fallback={YBLoadingCircleIcon}>
        <div className="dashboard-container">
          <AlertsListContainer />
        </div>
      </Suspense>
    );
  }
}
