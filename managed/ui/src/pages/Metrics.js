// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import CustomerMetricsPanel from '../components/metrics/CustomerMetricsPanel/CustomerMetricsPanel';

class Metrics extends Component {
  render() {
    return (
      <div>
        <CustomerMetricsPanel origin={"customer"}/>
      </div>
    );
  }
}

export default Metrics;
