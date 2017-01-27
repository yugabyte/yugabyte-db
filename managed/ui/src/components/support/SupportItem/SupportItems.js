// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Row, Col} from 'react-bootstrap';

export default class SupportItems extends Component {
  render() {
    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col lg={10} className="universe-table-header">
            <h3>Support</h3>
          </Col>
        </Row>
      </div>
    )
  }
}
