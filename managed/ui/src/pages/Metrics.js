// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import CustomerMetricsPanel from '../components/metrics/CustomerMetricsPanel';

class Metrics extends Component {
  render() {
    return (
      <div>
        <CustomerMetricsPanel />
      </div>
    );
  }
}

export default Metrics;
