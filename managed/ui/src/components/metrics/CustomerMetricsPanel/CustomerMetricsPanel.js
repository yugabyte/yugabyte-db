// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { GraphPanelHeaderContainer, GraphPanelContainer } from '../../metrics'

export default class CustomerMetricsPanel extends Component {
  componentWillMount() {
    this.props.fetchUniverseList();
  }

  render() {
    const {origin, nodePrefixes} = this.props;
    return (
      <GraphPanelHeaderContainer origin={origin}>
        <GraphPanelContainer type={"cql"} nodePrefixes={nodePrefixes}/>
        <GraphPanelContainer type={"redis"} nodePrefixes={nodePrefixes}/>
        <GraphPanelContainer type={"tserver"} nodePrefixes={nodePrefixes}/>
        <GraphPanelContainer type={"server"} nodePrefixes={nodePrefixes}/>
      </GraphPanelHeaderContainer>
    )
  }
}
