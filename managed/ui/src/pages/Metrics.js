// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { CustomerMetricsPanel } from '../components/metrics';
import Measure from 'react-measure';

class Metrics extends Component {
  state = {
    dimensions: {},
  }

  onResize(dimensions) {
    dimensions.width -= 2;
    this.setState({dimensions});
  }
  render() {
    return (
      <Measure onMeasure={this.onResize.bind(this)}>
        <CustomerMetricsPanel origin={"customer"} width={this.state.dimensions.width} />
      </Measure>
    );
  }
}

export default Metrics;
