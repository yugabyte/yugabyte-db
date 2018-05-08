// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Grid, Row, Col, Tab } from 'react-bootstrap';
import { Link } from 'react-router';
import { TableInfoPanel, YBTabsPanel } from '../../panels';
import { RegionMap, YBMapLegend } from '../../maps';
import './TableDetail.scss';
import { TableSchema } from '../../tables';
import { CustomerMetricsPanel } from '../../metrics';
import { isValidObject, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { getPrimaryCluster } from '../../../utils/UniverseUtils';

import { UniverseStatusContainer } from '../../universes';

export default class TableDetail extends Component {
  static propTypes = {
    universeUUID: PropTypes.string.isRequired,
    tableUUID: PropTypes.string.isRequired
  };

  componentWillMount() {
    const universeUUID = this.props.universeUUID;
    const tableUUID = this.props.tableUUID;
    this.props.fetchUniverseDetail(universeUUID);
    this.props.fetchTableDetail(universeUUID, tableUUID);
  }

  componentWillUnmount() {
    this.props.resetUniverseDetail();
    this.props.resetTableDetail();
  }

  render() {
    let tableInfoContent = <span/>;
    const {
      universe: { currentUniverse },
      tables: { currentTableDetail }
    } = this.props;
    if (getPromiseState(currentUniverse).isSuccess()) {
      const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
      if (isNonEmptyObject(primaryCluster)) {
        tableInfoContent = (
          <div>
            <Row className={"table-detail-row"}>
              <Col lg={4}>
                <TableInfoPanel tableInfo={currentTableDetail} />
              </Col>
              <Col lg={8} />
            </Row>
            <Row>
              <Col lg={12}>
                <RegionMap regions={primaryCluster.regions} type={"Table"} />
                <YBMapLegend title="Placement Policy" regions={primaryCluster.regions} />
              </Col>
            </Row>
          </div>
        );
      }
    }
    let tableSchemaContent = <span/>;
    if (isValidObject(currentTableDetail)) {
      tableSchemaContent = <TableSchema tableInfo={currentTableDetail}/>;
    }
    let tableMetricsContent = <span/>;
    if (isNonEmptyObject(currentUniverse) && isNonEmptyObject(currentTableDetail)) {
      const nodePrefixes = [currentUniverse.data.universeDetails.nodePrefix];
      const tableName = currentTableDetail.tableDetails.tableName;
      tableMetricsContent =
        (<CustomerMetricsPanel origin={"table"}
                               tableName={tableName}
                               nodePrefixes={nodePrefixes} />);
    }
    const tabElements = [
      <Tab eventKey={"overview"} title="Overview" key="overview-tab" mountOnEnter={true} unmountOnExit={true}>
        {tableInfoContent}
      </Tab>,
      <Tab eventKey={"schema"} title="Schema" key="tables-tab" mountOnEnter={true} unmountOnExit={true}>
        {tableSchemaContent}
      </Tab>,
      <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab" mountOnEnter={true} unmountOnExit={true}>
        {tableMetricsContent}
      </Tab>
    ];
    let tableName = "";
    if (isValidObject(currentTableDetail.tableDetails)) {
      tableName = currentTableDetail.tableDetails.tableName;
    }

    let universeState = <span/>;
    if (isNonEmptyObject(currentUniverse.data) && isNonEmptyObject(currentTableDetail.tableDetails)) {
      universeState = (
        <Col lg={10} sm={8} xs={6}>
          {/* UNIVERSE NAME */}
          <div className="universe-detail-status-container">
            <h2>
              <Link to={`/universes/${currentUniverse.data.universeUUID}`}>
                {currentUniverse.data.name}
              </Link>
              <span>
                <i class="fa fa-chevron-right"></i>
                <Link to={`/universes/${currentUniverse.data.universeUUID}?tab=tables`}>
                  Tables
                </Link>
                <i class="fa fa-chevron-right"></i>
                { tableName }
              </span>
            </h2>
            <UniverseStatusContainer currentUniverse={currentUniverse.data} showLabelText={true} refreshUniverseData={this.getUniverseInfo}/>
          </div>
        </Col>);
    }

    return (
      <Grid id="page-wrapper" fluid={true}>
        <Row className="header-row">
          {universeState}
        </Row>
        <Row>
          <Col lg={12}>
            <YBTabsPanel defaultTab={"schema"} id={"tables-tab-panel"}>
              { tabElements }
            </YBTabsPanel>
          </Col>
        </Row>
      </Grid>
    );
  }
}
