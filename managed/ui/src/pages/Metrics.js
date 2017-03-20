// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { CustomerMetricsPanelContainer } from '../components/metrics';

class Metrics extends Component {
  render() {
    return (
      <CustomerMetricsPanelContainer origin={"customer"}/>
    );
  }
}

export default Metrics;
