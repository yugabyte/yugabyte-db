// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Measure from 'react-measure';
import { GraphPanelHeaderContainer, GraphPanelContainer } from '../../metrics'

export default class CustomerMetricsPanel extends Component {
  state = {
    dimensions: {},
  }

  componentWillMount() {
    this.props.fetchUniverseList();
  }

  onResize(dimensions) {
    dimensions.width -= 2;
    this.setState({dimensions});
  }

  render() {
    const {origin, nodePrefixes} = this.props;
    return (
      <Measure onMeasure={this.onResize.bind(this)}>
        <GraphPanelHeaderContainer origin={origin}>
          <GraphPanelContainer width={this.state.dimensions.width} nodePrefixes={nodePrefixes} type={"proxies"} />
          <GraphPanelContainer width={this.state.dimensions.width} nodePrefixes={nodePrefixes} type={"server"} />
          <GraphPanelContainer width={this.state.dimensions.width} nodePrefixes={nodePrefixes} type={"cql"} />
          <GraphPanelContainer width={this.state.dimensions.width} nodePrefixes={nodePrefixes} type={"redis"} />
          <GraphPanelContainer width={this.state.dimensions.width} nodePrefixes={nodePrefixes} type={"tserver"} />
          <GraphPanelContainer width={this.state.dimensions.width} nodePrefixes={nodePrefixes} type={"lsmdb"} />
        </GraphPanelHeaderContainer>
      </Measure>
    );
  }
}
