// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { MetricsPanelContainer, GraphPanelHeaderContainer } from '../../containers/metrics'

export default class CustomerMetricsPanel extends Component {

  render() {
    return (
      <GraphPanelHeaderContainer origin="customer">
        <Row>
          <div className="panel-heading">
            YugaByte Server Stats
          </div>
          <Col lg={5}>
            <MetricsPanelContainer metricKey="cpu_usage" origin={"customer"} />
          </Col>
          <Col lg={5}>
            <MetricsPanelContainer metricKey="memory_usage" origin={"customer"} />
          </Col>
        </Row>
        <Row>
          <div className="panel-heading">
            Redis Stats
          </div>
          <Col lg={5}>
            <MetricsPanelContainer metricKey="redis_ops_latency" origin={"customer"} />
          </Col>
          <Col lg={5}>
            <MetricsPanelContainer metricKey="redis_rpcs_per_sec" origin={"customer"} />
          </Col>
        </Row>
      </GraphPanelHeaderContainer>
    );
  }
}
