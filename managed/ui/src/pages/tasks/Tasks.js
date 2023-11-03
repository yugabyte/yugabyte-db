// Copyright (c) YugaByte, Inc.
import { Component, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../../components/common/indicators';

export default class Tasks extends Component {
  render() {
    return (
      <Suspense fallback={YBLoadingCircleIcon}>
        <div className="dashboard-container">{this.props.children}</div>
      </Suspense>
    );
  }
}
