// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

import { UniverseRegionLocationPanelContainer,
   UniverseDisplayPanelContainer } from '../panels';
import './stylesheets/Dashboard.css'

export default class Dashboard extends Component {
  componentDidMount() {
    this.props.fetchUniverseList();
  }
  componentWillUnmount() {
    this.props.fetchUniverseList();
  }
  render() {
    return (
      <div id="page-wrapper" className="dashboard-container">
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
