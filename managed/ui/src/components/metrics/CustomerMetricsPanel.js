// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { GraphPanelHeaderContainer, GraphPanelContainer } from '../../containers/metrics'

export default class CustomerMetricsPanel extends Component {

  render() {
    const {origin} = this.props;
    return (
      <GraphPanelHeaderContainer origin={origin}>
        <GraphPanelContainer type={"redis"} nodePrefixes={this.props.nodePrefixes}/>
        <GraphPanelContainer type={"server"} nodePrefixes={this.props.nodePrefixes}/>
        <GraphPanelContainer type={"tserver"} nodePrefixes={this.props.nodePrefixes}/>
      </GraphPanelHeaderContainer>
    )
  }
}
