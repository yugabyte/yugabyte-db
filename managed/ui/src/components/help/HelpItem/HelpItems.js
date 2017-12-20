// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Row, Col} from 'react-bootstrap';

import gitterIcon from './images/gitter.svg';
import './HelpItems.scss';

export default class HelpItems extends Component {
  render() {
    return (
      <div id="page-wrapper" className="help-links">
        <h2 className="content-title">Help</h2>

        <Row>
          <Col lg={6}>
            <h4><i className="fa fa-globe"></i> Resources</h4>
          </Col>
          <Col lg={6}>
            <p>
              <a href="https://docs.yugabyte.com/" target="_blank" rel="noopener noreferrer">
                <i className="fa fa-book"></i> Documentation
              </a>
            </p>
            <p>
              <a href="https://github.com/yugabyte" target="_blank" rel="noopener noreferrer">
                <i className="fa fa-github"></i> GitHub
              </a>
            </p>
          </Col>
        </Row><br/>

        <Row>
          <Col lg={6}>
            <h4><i className="fa fa-support"></i> Support</h4>
          </Col>
          <Col lg={6}>
            <p>
              <a href="https://gitter.im/YugaByte/Lobby" target="_blank" rel="noopener noreferrer">
                <object className="logo svg-logo" data={gitterIcon} type="image/svg+xml" width="16">Icon</object> Gitter
              </a>
            </p>
            <p>
              <a href="https://www.youtube.com/channel/UCL9BhSLRowqQ1TyBndhiCEw" target="_blank" rel="noopener noreferrer">
                <i className="fa fa-youtube"></i> YouTube
              </a>
            </p>
            <p><a href="mailto:support@yugabyte.com"><i className="fa fa-envelope-o"></i> Email</a></p>
          </Col>
        </Row>

      </div>
    );
  }
}
