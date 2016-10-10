// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import HighlightedStatsPanelContainer from '../containers/panels/HighlightedStatsPanelContainer';
import UniverseGraphPanelContainer from '../containers/panels/UniverseGraphPanelContainer';
import UniverseCostBreakDownPanelContainer from '../containers/panels/UniverseCostBreakDownPanelContainer';
import UniverseRegionLocationPanelContainer from '../containers/panels/UniverseRegionLocationPanelContainer';
import { Col } from 'react-bootstrap';

export default class Dashboard extends Component {

  render() {
    return (
      <div id="page-wrapper" className="dashboard-widget-container">
        <HighlightedStatsPanelContainer />
        <UniverseGraphPanelContainer />
          <Col lg={6} md={6} >
            <UniverseCostBreakDownPanelContainer />
          </Col>
          <Col lg={6}>
            <UniverseRegionLocationPanelContainer {...this.props}/>
          </Col>
      </div>
    );
    }
  }
