// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

import { MetricsPanelContainer, GraphPanelHeaderContainer } from '../../containers/metrics'

export default class GraphPanel extends Component {
  static propTypes = {
    origin: PropTypes.oneOf(['customer', 'universe']).isRequired,
    universeUUID: PropTypes.string
  };

  static defaultProps = {
    universeUUID: null
  }

  render() {
    return (
      <GraphPanelHeaderContainer>
        <MetricsPanelContainer metricKey="redis_ops_latency" {...this.props} />
        <MetricsPanelContainer metricKey="redis_rpcs_per_sec" {...this.props} />
        <MetricsPanelContainer metricKey="tserver_ops_latency" {...this.props} />
        <MetricsPanelContainer metricKey="tserver_rpcs_per_sec" {...this.props} />
        <MetricsPanelContainer metricKey="cpu_usage" {...this.props} />
        <MetricsPanelContainer metricKey="memory_usage" {...this.props} />
        <MetricsPanelContainer metricKey="disk_iops" {...this.props} />
        <MetricsPanelContainer metricKey="network_bytes" {...this.props} />
      </GraphPanelHeaderContainer>
    );
  }
}
