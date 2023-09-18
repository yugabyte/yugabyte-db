// Copyright (c) YugaByte, Inc.

import { Component, lazy, Suspense } from 'react';
import Measure from 'react-measure';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const CustomerMetricsPanelContainer = lazy(() =>
  import('../components/metrics/CustomerMetricsPanel/CustomerMetricsPanelContainer')
);

class Metrics extends Component {
  state = {
    dimensions: {}
  };

  onResize(dimensions) {
    this.setState({ dimensions });
  }
  render() {
    return (
      <Suspense fallback={YBLoadingCircleIcon}>
        <Measure onMeasure={this.onResize.bind(this)}>
          <div className="dashboard-container">
            <CustomerMetricsPanelContainer
              origin={'customer'}
              width={this.state.dimensions.width}
            />
          </div>
        </Measure>
      </Suspense>
    );
  }
}

export default Metrics;
