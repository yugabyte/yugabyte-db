// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Col } from 'react-bootstrap';

import { RegionMapLegend } from '../maps';
import { UniverseRegionLocationPanelContainer,
   UniverseDisplayPanelContainer } from '../panels';
import './stylesheets/Dashboard.css'

export default class Dashboard extends Component {
  componentDidMount() {
    this.props.fetchUniverseList();
  }
  componentWillUnmount() {
    this.props.resetUniverseList();
  }
  render() {
    return (
      <div id="page-wrapper" className="dashboard-container">
        <Col lg={12}>
          <UniverseDisplayPanelContainer {...this.props}/>
        </Col>
        <Col lg={12}>
          <UniverseRegionLocationPanelContainer {...this.props}/>
          <RegionMapLegend />
        </Col>
      </div>
    );
    }
  }
