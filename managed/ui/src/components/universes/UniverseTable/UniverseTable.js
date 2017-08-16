// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, ListGroup, ListGroupItem } from 'react-bootstrap';
import { Link } from 'react-router';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { isObject } from 'lodash';
import { isValidObject, isNonEmptyArray } from '../../../utils/ObjectUtils';
import './UniverseTable.scss';
import {UniverseReadWriteMetrics} from '../../metrics';
import {YBCost} from '../../common/descriptors';
import {UniverseStatusContainer} from '../../universes'
var moment = require('moment');

export default class UniverseTable extends Component {

  componentWillMount() {
    this.props.fetchUniverseMetadata();
    this.props.fetchUniverseTasks();
    this.props.universeReadWriteData();
  }

  componentWillUnmount() {
    this.props.resetUniverseTasks();
  }

  render() {
    var self = this;
    const { universe: { universeList }, universeReadWriteData, tasks } = this.props;

    if (!isObject(universeList) || !isNonEmptyArray(universeList.data)) {
      return <h5>No universes defined.</h5>;
    }

    var universeRowItem =
      universeList.data.sort((a, b) => {
        return Date.parse(a.creationDate) < Date.parse(b.creationDate)
      }).map(function (item, idx) {
        var universeTaskUUIDs = [];
        if (isNonEmptyArray(tasks.customerTaskList)) {
          universeTaskUUIDs = tasks.customerTaskList.map(function(taskItem){
            if (taskItem.universeUUID === item.universeUUID) {
              return {"id": taskItem.id, "data": taskItem, "universe": item.universeUUID};
            } else {
              return null;
            }
          }).filter(Boolean).sort(function(a, b){
            return a.data.createTime < b.data.createTime;
          })
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
    )
  }
}

class YBUniverseItem extends Component {
  render() {
    const { universe } = this.props;
    return (
      <div className={"universe-list-item"}>
        <ListGroupItem >
          <Row>
            <Col sm={4}>
              <Link to={`/universes/${universe.universeUUID}`}><div className={"universe-name-cell"}>{universe.name}</div></Link>
            </Col>
            <Col sm={2} className="universe-create-date-container">
              <div >Created: {moment(universe.creationDate).format("MM/DD/YYYY, hh:mm a")}</div>
            </Col>
            <Col sm={6} className={"list-universe-status-container"}>
              <UniverseStatusContainer currentUniverse={universe} showLabelText={true}/>
            </Col>
          </Row>
          <Row className={"universe-list-detail-item"}>
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
    var isMultiAz = userIntent.isMultiAZ ? "Multi AZ" : "Single AZ";
    var regionList = universe.regions && universe.regions.map(function(regionItem, idx){
      return <span key={idx}>{regionItem.name}</span>
    })

    return (
      <div >
        <Row className={"cell-position-detail"}>
          <Col sm={2} className={"cell-num-nodes"}>{userIntent && userIntent.numNodes} Nodes</Col>
          <Col sm={10}>
            <span className={"cell-provider-name"}>{universe.provider && universe.provider.name}</span>
            <span className={"cell-multi-az"}>({isMultiAz})</span>
          </Col>
        </Row>
        <Row className={"cell-position-detail"}>
          <Col sm={2} className={"cell-num-nodes"}>{userIntent.regionList.length} Regions</Col>
          <Col sm={10}>{regionList}</Col>
        </Row>
      </div>
    )
  }
}

class CellResourcesPanel extends Component {

  render() {
    const {universe: {universeUUID, pricePerHour, iostat_read_count, iostat_write_count}} = this.props;
    var averageReadRate = 0;
    var averageWriteRate = 0;

    if (isValidObject(iostat_read_count)) {
      var readMetricArray = iostat_read_count.y;
      var sum = readMetricArray.reduce(function (a, b) {
        return parseFloat(a) + parseFloat(b);
      });
      averageReadRate = (sum / readMetricArray.length).toFixed(2);
    }
    if (isValidObject(iostat_write_count)) {
      var writeMetricArray = iostat_write_count.y;
      sum = writeMetricArray.reduce(function (a, b) {
        return parseFloat(a) + parseFloat(b);
      });
      averageWriteRate = (sum / writeMetricArray.length).toFixed(2);
    }

    return (
      <div>
        <Col sm={4}>
          <div className={"cell-cost-item"}>
            <YBCost value={pricePerHour} multiplier={"month"}/>
          </div>
          Monthly Cost
        </Col>
        <Col sm={8}>
          <Row>
            <Row>
              <Col sm={6} className={"cell-bold-label"}>
                Read <span className="cell-bold-letters">{averageReadRate}</span>
              </Col>
              <Col sm={6} className={"cell-chart-container"}>
                <UniverseReadWriteMetrics {...this.props} graphIndex={`${universeUUID}-read`} type={"read"} />
              </Col>
            </Row>
            <Row >
              <Col sm={6} className={"cell-bold-label"}>
                Write <span className="cell-bold-letters">{averageWriteRate}</span>
              </Col>
              <Col sm={6} className={"cell-chart-container"}>
                <UniverseReadWriteMetrics {...this.props} graphIndex={`${universeUUID}-write`} type={"write"}/>
              </Col>
            </Row>
          </Row>
        </Col>
      </div>
    )
  }
}
