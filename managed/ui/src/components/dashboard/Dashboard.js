// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

import { UniverseRegionLocationPanelContainer, HighlightedStatsPanelContainer,  
  UniverseDisplayPanelContainer } from '../panels';
import './stylesheets/Dashboard.scss';

export default class Dashboard extends Component {

  render() {
    return (
      <div id="page-wrapper" className="dashboard-container">
        <div className="dashboard-stats">
          <HighlightedStatsPanelContainer />
        </div>
        <UniverseDisplayPanelContainer {...this.props}/>
        <Row>
          <Col lg={12}>
            <UniverseRegionLocationPanelContainer {...this.props}/>
          </Col>
        </Row>
      </div>
    );
  }
  }
