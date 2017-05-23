// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col, Tab, ButtonGroup, DropdownButton, MenuItem } from 'react-bootstrap';
import { Link } from 'react-router';
import { YBLabelWithIcon } from '../../common/descriptors';
import { TableInfoPanel, YBTabsPanel } from '../../panels';
import { RegionMap, YBMapLegend } from '../../maps';
import { isValidObject } from '../../../utils/ObjectUtils';
import {getPromiseState} from 'utils/PromiseUtils';
import './TableDetail.scss';
import {ItemStatus} from '../../common/indicators';
import {TableSchema} from '../../tables';

export default class TableDetail extends Component {
  static propTypes = {
    universeUUID: PropTypes.string.isRequired,
    tableUUID: PropTypes.string.isRequired
  }

  componentWillMount() {
    var universeUUID = this.props.universeUUID;
    var tableUUID = this.props.tableUUID;
    this.props.fetchUniverseDetail(universeUUID);
    this.props.fetchTableDetail(universeUUID, tableUUID);
  }

  componentWillUnmount() {
    this.props.resetTableDetail();
  }
  render() {
    var tableInfoContent = <span/>;
    const {universe: {currentUniverse}, tables: {currentTableDetail}} = this.props;
    if (getPromiseState(currentUniverse).isSuccess()) {
      tableInfoContent =
        <div>
          <Row className={"table-detail-row"}>
            <Col lg={4}>
              <TableInfoPanel tableInfo={currentTableDetail}/>
            </Col>
            <Col lg={8}>
            </Col>
          </Row>
          <Row>
            <Col lg={12}>
              <RegionMap regions={currentUniverse.data.regions} type={"Root"} />
              <YBMapLegend title="Placement Policy" regions={currentUniverse.data.regions}/>
            </Col>
          </Row>
        </div>
    }
    var tableSchemaContent = <span/>;
    if (isValidObject(currentTableDetail)) {
      tableSchemaContent = <TableSchema tableInfo={currentTableDetail}/>
    }
    var tabElements = [
      <Tab eventKey={"overview"} title="Overview" key="overview-tab">
        {tableInfoContent}
      </Tab>,
      <Tab eventKey={"schema"} title="Schema" key="tables-tab">
        {tableSchemaContent}
      </Tab>,
      <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab"/>
    ];
    var tableName = "";
    if (isValidObject(currentTableDetail.tableDetails)) {
      tableName = currentTableDetail.tableDetails.tableName;
    }
    var universeUUID = this.props.universeUUID;
    return (
      <Grid id="page-wrapper" fluid={true}>
        <Row className="header-row">
          <Col lg={10}>
            <div className="detail-label-small">
              <Link to="/universes">
                <YBLabelWithIcon icon="fa fa-chevron-right fa-fw">
                  Universes
                </YBLabelWithIcon>
              </Link>
              <Link to={`/universes/${universeUUID}?tab=tables`}>
                <YBLabelWithIcon icon="fa fa-chevron-right fa-fw">
                  Tables
                </YBLabelWithIcon>
              </Link>
            </div>
            <div>
              <h2>
                { tableName }
                <ItemStatus showLabelText={true} />
              </h2>
            </div>
          </Col>
          <Col lg={2} className="page-action-buttons">
            <ButtonGroup className="universe-detail-btn-group">
              <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
                <MenuItem eventKey="1">
                  <YBLabelWithIcon icon="fa fa-trash">
                    Drop Table
                  </YBLabelWithIcon>
                </MenuItem>
              </DropdownButton>
            </ButtonGroup>
          </Col>
        </Row>
        <Row>
          <Col lg={12}>
            <YBTabsPanel activeTab={"schema"} id={"tables-tab-panel"}>
              { tabElements }
            </YBTabsPanel>
          </Col>
        </Row>
      </Grid>
    );
  }
}
