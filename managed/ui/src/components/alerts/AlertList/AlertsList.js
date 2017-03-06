// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Row, Col} from 'react-bootstrap';

export default class AlertsList extends Component {
  render() {
    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col lg={10}>
            <h3>Alerts</h3>
          </Col>
        </Row>
      </div>
    )
  }
}
