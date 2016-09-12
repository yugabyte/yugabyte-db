// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import HighlightedStatsPanelContainer from '../containers/panels/HighlightedStatsPanelContainer';
import UniverseGraphPanelContainer from '../containers/panels/UniverseGraphPanelContainer';
import UniverseCostBreakDownPanelContainer from '../containers/panels/UniverseCostBreakDownPanelContainer';
import UniverseRegionLocationPanelContainer from '../containers/panels/UniverseRegionLocationPanelContainer';
import { Col } from 'react-bootstrap';

export default class Dashboard extends Component {
  static contextTypes = {
    router: PropTypes.object
  };

  constructor(props) {
    super(props);
    props.fetchUniverseList();
  }

  componentWillUnmount() {
    this.props.resetUniverseList();
  }

  render() {
    return (
      <div id="page-wrapper" className="dashboard-widget-container">
        <HighlightedStatsPanelContainer />
        <UniverseGraphPanelContainer />
          <Col lg={6} md={6} >
            <UniverseCostBreakDownPanelContainer />
          </Col>
          <Col lg={6}>
            <UniverseRegionLocationPanelContainer />
          </Col>
      </div>
    );
    }
  }
