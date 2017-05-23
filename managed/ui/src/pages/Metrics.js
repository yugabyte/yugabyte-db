// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { CustomerMetricsPanel } from '../components/metrics';

class Metrics extends Component {
  render() {
    return (
      <CustomerMetricsPanel origin={"customer"}/>
    );
  }
}

export default Metrics;
