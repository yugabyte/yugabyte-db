// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Row, Col} from 'react-bootstrap';

export default class HelpItems extends Component {
  render() {
    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col lg={12}>
            <h2>Help</h2>
          </Col>
        </Row>

        <Row>
          <Col lg={3}>
            <h3>
              <a href="#">
                <i className="fa fa-book"></i> Documentation
              </a>
            </h3>
          </Col>

          <Col lg={3}>
            <h3>
              <a href="#">
                <i className="fa fa-support"></i> Support Desk
              </a>
            </h3>
          </Col>

          <Col lg={3}>
            <h3>
              <a href="#">
                <i className="fa fa-envelope-o"></i> Contact Us
              </a>
            </h3>
          </Col>
        </Row>
      </div>
    )
  }
}
