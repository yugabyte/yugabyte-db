// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { HighlightedStatsPanelContainer, UniverseRegionLocationPanelContainer,
   UniverseDisplayPanelContainer } from '../../containers/panels';
import { Col } from 'react-bootstrap';
import { SupportedRegionsMapLegend } from '../maps';
export default class Dashboard extends Component {
  componentDidMount() {
    this.props.fetchUniverseList();
  }
  componentWillUnmount() {
    this.props.resetUniverseList();
  }
  render() {
    return (
      <div id="page-wrapper" className="dashboard-widget-container">
        <HighlightedStatsPanelContainer />
        <Col lg={12}>
          <UniverseDisplayPanelContainer {...this.props}/>
        </Col>
        <Col lg={9} className="supportedRegionMap">
          <UniverseRegionLocationPanelContainer {...this.props}/>
        </Col>
        <Col lg={3}>
          <SupportedRegionsMapLegend />
        </Col>
      </div>
    );
    }
  }
