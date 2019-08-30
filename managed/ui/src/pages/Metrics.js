// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { CustomerMetricsPanelContainer } from '../components/metrics';
import Measure from 'react-measure';

class Metrics extends Component {
  state = {
    dimensions: {},
  }

  onResize(dimensions) {
    this.setState({dimensions});
  }
  render() {
    return (
      <div className="dashboard-container">
        <Measure onMeasure={this.onResize.bind(this)}>
          <CustomerMetricsPanelContainer origin={"customer"} width={this.state.dimensions.width} />
        </Measure>
      </div>
    );
  }
}

export default Metrics;
