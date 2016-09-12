// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import YBPanelItem from '../YBPanelItem';
import { Col } from 'react-bootstrap';

export default class UniverseRegionLocationPanel extends Component {
  render() {
    return (
          <YBPanelItem name="All Supported Regions">
            <div className="x_content">
              <div className="dashboard-widget-content">
                <Col md={4} className="hidden-small">
                  <table className="countries_list">
                    <tbody></tbody>
                  </table>
                </Col>
                <Col md={8} sm={12} xs={12} id="world-map-gdp"></Col>
              </div>
            </div>
          </YBPanelItem>
    )
  }
}
