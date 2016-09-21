// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

export default class UniverseGraphPanel extends Component {
  render() {
    const { universe: { loading } } = this.props;

    if (loading) {
      return <div className="container">Loading...</div>;
    }

    return (
      <Row className="graph-panel-container" >
        <Col lg={12}>
          <div className="dashboard_graph">
            <Row className="x_title">
              <Col md={6}>
                <h3>Total reads/writes <small>per second, by universe</small></h3>
              </Col>
              <Col md={12}>
                <div id="reportrange" className="pull-right" >
                  <i className="glyphicon glyphicon-calendar fa fa-calendar"></i>
                  <span>December 30, 2015 - September 07, 2016</span>
                    <b className="caret"></b>
                </div>
              </Col>
            </Row>
            <Col md={3}>
              <div className="x_title">
                <h2>
                  Universes
                </h2>
                <div className="clearfix"></div>
              </div>
            </Col>
            <div className="clearfix"></div>
          </div>
        </Col>
      </Row>
    )
  }
}
