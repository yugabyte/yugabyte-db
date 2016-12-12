// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { GraphPanelHeaderContainer, GraphPanelContainer } from '../../containers/metrics'

export default class CustomerMetricsPanel extends Component {

  render() {
    return (
      <GraphPanelHeaderContainer>
        <GraphPanelContainer type={"redis"} nodePrefixes={this.props.nodePrefixes}/>
        <GraphPanelContainer type={"server"} nodePrefixes={this.props.nodePrefixes}/>
      </GraphPanelHeaderContainer>
    )
  }
}
