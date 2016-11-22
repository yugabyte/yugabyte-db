// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import GraphPanelHeaderContainer from '../containers/GraphPanelHeaderContainer';
import MetricsPanelContainer from '../containers/MetricsPanelContainer'

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
        <MetricsPanelContainer metricKey="memory_usage" {...this.props} />
        <MetricsPanelContainer metricKey="cpu_usage_system" {...this.props} />
        <MetricsPanelContainer metricKey="cpu_usage_user" {...this.props} />
      </GraphPanelHeaderContainer>
    );
  }
}
