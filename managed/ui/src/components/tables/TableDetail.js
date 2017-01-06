// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col, Tab } from 'react-bootstrap';
import { Link } from 'react-router';
import { YBLabelWithIcon } from '../common/descriptors';
import { YBTabsPanel } from '../panels';

export default class TableDetail extends Component {
  static propTypes = {
    universeUUID: PropTypes.string.isRequired,
    tableUUID: PropTypes.string.isRequired
  }

  componentWillMount() {
    var universeUUID = this.props.universeUUID;
    var tableUUID = this.props.tableUUID;
    this.props.fetchTableDetail(universeUUID, tableUUID);
  }

  componentWillUnmount() {
    this.props.resetTableDetail();
  }
  render() {
    var tabElements = [
      <Tab eventKey={"overview"} title="Overview" key="overview-tab"/>,
      <Tab eventKey={"tables"} title="Schema" key="tables-tab"/>,
      <Tab eventKey={"nodes"} title="Query" key="nodes-tab"/>,
      <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab"/>
      ];
    var universeUUID = this.props.universeUUID;
    return (
      <Grid id="page-wrapper" fluid={true}>
        <Row className="header-row">
          <Col lg={10}>
            <div className="detail-label-small">
              <Link to="/universes">
                <YBLabelWithIcon icon="fa fa-chevron-left fa-fw">
                  Universes
                </YBLabelWithIcon>
              </Link>
              <Link to={`universes/${universeUUID}?tab=tables`}>
                <YBLabelWithIcon icon="fa fa-chevron-left fa-fw">
                  Tables
                </YBLabelWithIcon>
              </Link>
            </div>
          </Col>
        </Row>
        <Row>
          <YBTabsPanel activeTab={"overview"} id={"universe-tab-panel"}>
            { tabElements }
          </YBTabsPanel>
        </Row>
      </Grid>
    );
  }
}
