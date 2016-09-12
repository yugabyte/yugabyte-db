// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import UniverseTableContainer from '../containers/UniverseTableContainer';
import YBPanelItem from './YBPanelItem';
import { Row, Col } from 'react-bootstrap';
import UniverseModalContainer from '../containers/UniverseModalContainer';

export default class ListUniverse extends Component {
  render() {
    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col lg={10} className="universe-table-header">
            <h3>Universes<small> Status and details</small></h3>
          </Col>
          <Col lg={1} className="universe-table-header-action">
            <UniverseModalContainer type="Create" />
          </Col>
        </Row>
        <div>
          <YBPanelItem name="Universe List">
            <UniverseTableContainer />
          </YBPanelItem>
        </div>
      </div>
    )
  }
}
