// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, ListGroup, ListGroupItem } from 'react-bootstrap';
import { Link } from 'react-router';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { isObject } from 'lodash';
import { isNonEmptyArray, isNonEmptyObject } from '../../../utils/ObjectUtils';
import './UniverseTable.scss';
import {UniverseReadWriteMetrics} from '../../metrics';
import {YBCost} from '../../common/descriptors';
import {UniverseStatusContainer} from '../../universes';
const moment = require('moment');

export default class UniverseTable extends Component {

  componentWillMount() {
    this.props.fetchUniverseMetadata();
    this.props.fetchUniverseTasks();
  }

  componentWillUnmount() {
    this.props.resetUniverseTasks();
  }

  render() {
    const self = this;
    const { universe: { universeList }, universeReadWriteData, tasks } = this.props;
    if (!isObject(universeList) || !isNonEmptyArray(universeList.data)) {
      return <h5>No universes defined.</h5>;
    }
    const universeRowItem = universeList.data.sort((a, b) => {
      return Date.parse(a.creationDate) < Date.parse(b.creationDate);
    }).map(function (item, idx) {
      let universeTaskUUIDs = [];
      if (isNonEmptyArray(tasks.customerTaskList)) {
        universeTaskUUIDs = tasks.customerTaskList.map(function(taskItem){
          if (taskItem.universeUUID === item.universeUUID) {
            return {"id": taskItem.id, "data": taskItem, "universe": item.universeUUID};
          } else {
            return null;
          }
        }).filter(Boolean).sort(function(a, b){
          return a.data.createTime < b.data.createTime;
        });
      }
      return (
        <YBUniverseItem {...self.props} key={idx} universe={item} idx={idx}
                        taskId={universeTaskUUIDs} universeReadWriteData={universeReadWriteData} />
      );
    });
    return (
      <ListGroup>
        {universeRowItem}
      </ListGroup>
    );
  }
}

class YBUniverseItem extends Component {
  render() {
    const { universe } = this.props;
    return (
      <div className={"universe-list-item"}>
        <ListGroupItem >
          <Row className={"universe-list-item-name-status"}>
            <Col sm={6}>
              <Link to={`/universes/${universe.universeUUID}`}><div className={"universe-name-cell"}>{universe.name}</div></Link>
            </Col>
            <Col sm={2} className="universe-create-date-container">
              <div >Created: {moment(universe.creationDate).format("MM/DD/YYYY, hh:mm a")}</div>
            </Col>
            <Col sm={4} className={"list-universe-status-container"}>
              <UniverseStatusContainer currentUniverse={universe} showLabelText={true}/>
            </Col>
          </Row>
          <Row className={"universe-list-item-detail"}>
            <Col sm={7}>
              <CellLocationPanel {...this.props}/>
            </Col>
            <Col sm={5}>
              <CellResourcesPanel {...this.props}/>
            </Col>
          </Row>
        </ListGroupItem>
      </div>
    );
  }
}


class CellLocationPanel extends Component {
  render() {
    const {universe, universe: {universeDetails: {userIntent}}} = this.props;
    const regionList = universe.regions && universe.regions.map(function(regionItem, idx){
      return <span key={idx}>{regionItem.name}</span>;
    });
    return (
      <div >
        <Row className={"cell-position-detail"}>
          <Col sm={2} className={"cell-num-nodes"}>{userIntent && userIntent.numNodes} Nodes</Col>
          <Col sm={10}>
            <span className={"cell-provider-name"}>{universe.provider && universe.provider.name}</span>
          </Col>
        </Row>
        <Row className={"cell-position-detail"}>
          <Col sm={2} className={"cell-num-nodes"}>{userIntent.regionList.length} Regions</Col>
          <Col sm={10}>{regionList}</Col>
        </Row>
      </div>
    );
  }
}

class CellResourcesPanel extends Component {

  render() {

    const {universe: {universeUUID, pricePerHour, readData, writeData}} = this.props;
    let averageReadRate = Number(0).toFixed(2);
    let averageWriteRate = Number(0).toFixed(2);
    if (isNonEmptyObject(readData)) {
      const readMetricArray = readData.y;
      const sum = readMetricArray.reduce(function (a, b) {
        return parseFloat(a) + parseFloat(b);
      });
      averageReadRate = (sum / readMetricArray.length).toFixed(2);
    }

    if (isNonEmptyObject(writeData)) {
      const writeMetricArray = writeData.y;
      const sum = writeMetricArray.reduce(function (a, b) {
        return parseFloat(a) + parseFloat(b);
      });
      averageWriteRate = (sum / writeMetricArray.length).toFixed(2);
    }

    return (
      <Row>
        <Col md={5}>
          <div className="cell-chart-container">
            <UniverseReadWriteMetrics {...this.props} graphIndex={`${universeUUID}-read`} readData={readData} writeData={writeData}/>
          </div>
        </Col>
        <Col md={4} className="cell-read-write">
          <div className="cell-read-write-row">
            <span className="legend-square read-color" />
            <span className="metric-label-type">Read </span>
            <span className="label-type-identifier">ops/sec</span>
            <span className="cell-read-write-value">
              {averageReadRate}
              <span className="metric-value-label">&nbsp;avg</span>
            </span>
          </div>
          <div className="cell-read-write-row">
            <span className="legend-square write-color" />
            <span className="metric-label-type">Write </span>
            <span className="label-type-identifier">ops/sec</span>
            <span className="cell-read-write-value">
              {averageWriteRate}
              <span className="metric-value-label">&nbsp;avg</span>
            </span>
          </div>
        </Col>

        <Col md={3} className="cell-cost">
          <div className="cell-cost-value">
            <YBCost value={pricePerHour} multiplier="month"/>
          </div>
          /month
        </Col>
      </Row>
    );
  }
}
