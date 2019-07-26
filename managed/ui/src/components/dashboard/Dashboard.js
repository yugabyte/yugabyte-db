// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

import { UniverseRegionLocationPanelContainer, HighlightedStatsPanelContainer,
  UniverseDisplayPanelContainer } from '../panels';
import './stylesheets/Dashboard.scss';
import { isAvailable, showOrRedirect } from 'utils/LayoutUtils';

export default class Dashboard extends Component {
  render() {
    const { customer : { currentCustomer }} = this.props;
    showOrRedirect(currentCustomer.data.features, "menu.dashboard");

    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col xs={6}>
            <h2 className="content-title">Universes</h2>
          {isAvailable(currentCustomer.data.features, "main.stats") && <div className="dashboard-stats">
            <HighlightedStatsPanelContainer />
          </div>}
          </Col>
        </Row>
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
