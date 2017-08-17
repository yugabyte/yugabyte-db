// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Row, Col} from 'react-bootstrap';

import './HelpItems.scss';

export default class HelpItems extends Component {
  render() {
    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col lg={12}>
            <h2>Help</h2>
          </Col>
        </Row>
        <hr/>

        <Row>
          <Col lg={3}>
            <h4><i className="fa fa-support"></i> Support</h4>
          </Col>
          <Col lg={9} className="help-links">
            <p><a href=""><i className="fa fa-slack"></i> Slack Channel</a></p>
            <p><a href="mailto:support@yugabyte.com"><i className="fa fa-envelope-o"></i> Support Email</a></p>
          </Col>
        </Row>
        <hr/>

        <Row>
          <Col lg={3}>
            <h4><i className="fa fa-book"></i> Documentation</h4>
          </Col>
          <Col lg={9} className="help-links">
            Coming soon&hellip;
          </Col>
        </Row>
        <hr/>
      </div>
    );
  }
}
