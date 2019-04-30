// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { CustomerMetricsPanelContainer } from '../components/metrics';
import Measure from 'react-measure';

class Metrics extends Component {
  state = {
    dimensions: {},
  }

  onResize(dimensions) {
    dimensions.width -= 20;
    this.setState({dimensions});
  }
  render() {
    return (
      <Measure onMeasure={this.onResize.bind(this)}>
        <CustomerMetricsPanelContainer origin={"customer"} width={this.state.dimensions.width} />
      </Measure>
    );
  }
}

export default Metrics;
