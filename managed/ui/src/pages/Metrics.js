// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { CustomerMetricsPanelContainer } from '../components/metrics';

class Metrics extends Component {
  render() {
    return (
      <div>
        <CustomerMetricsPanelContainer origin={"customer"}/>
      </div>
    );
  }
}

export default Metrics;
