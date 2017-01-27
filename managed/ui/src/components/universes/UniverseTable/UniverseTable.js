// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, ListGroup, ListGroupItem, ProgressBar } from 'react-bootstrap';
import { Link } from 'react-router';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { isValidObject, isValidArray } from '../../../utils/ObjectUtils';
import './UniverseTable.scss';
import {UniverseReadWriteMetrics} from '../../metrics';
import {YBCost} from '../../common/descriptors';

export default class UniverseTable extends Component {

  componentWillMount() {
    this.props.fetchUniverseList();
    this.props.fetchUniverseTasks();
    this.props.universeReadWriteData();
  }

  componentWillUnmount() {
    this.props.resetUniverseList();
    this.props.resetUniverseTasks();
    this.props.resetTaskProgress();
  }

  render() {
    var self = this;
    const { universe: { universeList, universeTasks, loading }, universeReadWriteData } = this.props;
    if (loading) {
      return <div className="container">Loading...</div>;
    }

    var universeRowItem =
      universeList.map(function (item, idx) {
        var universeTaskUUIDs = [];
        if (isValidObject(universeTasks) && universeTasks[item.universeUUID] !== undefined) {
        universeTaskUUIDs = universeTasks[item.universeUUID].map(function (task) {
          return {"id": task.id, "data": task, "universe": item.universeUUID};
        });
      }
        self.props.fetchCurrentTaskList(universeTaskUUIDs);
        return <YBUniverseItem {...self.props} key={idx} universe={item} idx={idx}
                               taskId={universeTaskUUIDs} universeReadWriteData={universeReadWriteData} />
      });
    return (
      <div className="row">
        <ListGroup>
          {universeRowItem}
        </ListGroup>
      </div>
    )
  }
}

class YBUniverseItem extends Component {
  render() {
    const {universe, taskId} = this.props;
    var updateProgressStatus = false;
    var updateSuccessStatus = false;
    var currentStatusItem = "";
    if (isValidObject(universe.universeDetails.updateInProgress)) {
      updateProgressStatus = universe.universeDetails.updateInProgress;
    }
    if (isValidObject(universe.universeDetails.updateSucceeded)) {
      updateSuccessStatus = universe.universeDetails.updateSucceeded;
    }
    var percentComplete = 0 ;
    if (isValidArray(taskId) && isValidObject(taskId[0].data)) {
      percentComplete = taskId[0].data.percentComplete;
    }
    if (!updateProgressStatus && !updateSuccessStatus) {
      currentStatusItem = <div className={"status-failure-container"}>
                            <div className={"status-failure-item"}>
                              <i className={"fa fa-exclamation fa-fw"}/>
                            </div>
                            <div className={"status-failure-name"}>
                              Provisioning Error
                            </div>
                           </div>;
    } else if (updateSuccessStatus) {
      currentStatusItem = <div className={"status-success-container"}>
                            <div className={"status-success-item"}>
                            <i className={"fa fa-check fa-fw"}/>
                            </div>
                            <div className="status-success-label"> Ready</div>
                          </div>;
    } else {
      currentStatusItem =
        <div className={"status-pending"}>
          <Row className={"status-pending-display-container"}>
            <span className={"status-pending-name"}>{percentComplete} % complete&nbsp;</span>
            <i className={"fa fa fa-spinner fa-spin"}/>
            <Col className={"status-pending-name"}>
              Pending...
            </Col>
          </Row>
          <Row  className={"status-pending-progress-container "}>
            <ProgressBar className={"pending-action-progress"} now={percentComplete}/>
          </Row>
        </div>;
    }

    return (
      <div className={"universe-list-item"}>
        <ListGroupItem >
          <Row>
            <Col lg={6}>
              <Link to={`/universes/${universe.universeUUID}`}><div className={"universe-name-cell"}>{universe.name}</div></Link>
            </Col>
            <Col lg={6}>
              {currentStatusItem}
            </Col>
          </Row>
          <Row className={"universe-list-detail-item"}>
            <Col lg={8}>
              <CellLocationPanel {...this.props}/>
            </Col>
            <Col lg={4}>
              <CellResourcesPanel {...this.props}/>
            </Col>
          </Row>
        </ListGroupItem>
      </div>
    )
  }
}


class CellLocationPanel extends Component {
  render() {
    const {universe, universe: {universeDetails: {userIntent}}} = this.props;
    var isMultiAz = userIntent.isMultiAZ ? "MutliAZ" : "Single AZ";
    var regionList = universe.regions.map(function(regionItem, idx){
      return <span key={idx}>{regionItem.name}</span>
    })

    return (
      <div >
        <Row className={"cell-position-detail"}>
          <Col lg={3} className={"cell-num-nodes"}>{userIntent.numNodes} Nodes</Col>
          <Col lg={9}>
            <span className={"cell-provider-name"}>{universe.provider.name}</span>
            <span className={"cell-multi-az"}>{isMultiAz}</span>
          </Col>
        </Row>
        <Row className={"cell-position-detail"}>
          <Col lg={3} className={"cell-num-nodes"}>{userIntent.regionList.length} Regions</Col>
          <Col lg={9}>{regionList}</Col>
        </Row>
      </div>
    )
  }
}

class CellResourcesPanel extends Component {

  render() {
    const {universe: {pricePerHour, universeUUID}, graph: {universeMetricList}} = this.props;
    var averageReadRate = 0;
    var averageWriteRate = 0;
    var disk_iops =universeMetricList.disk_iops;
    if (isValidObject(disk_iops) && isValidArray(disk_iops.data)) {
      var readMetricArray = disk_iops.data[0].y;
      var sum = readMetricArray.reduce(function(a, b) { return a + b; });
      averageReadRate = (sum / readMetricArray.length).toFixed(2);
      var writeMetricArray = disk_iops.data[1].y;
      sum = writeMetricArray.reduce(function(a, b) { return parseFloat(a) + parseFloat(b); });
      averageWriteRate = (sum / writeMetricArray.length).toFixed(2);
    }
    return (
      <div>
        <Col lg={4}>
          <div className={"cell-cost-item"}>
            <YBCost value={pricePerHour} multiplier={"month"}/>
          </div>
          <div>Estimated Cost</div>
          <div>Per Month</div>
        </Col>
        <Col lg={8}>
          <Row>
            <Row>
              <Col lg={6} className={"cell-bold-label"}>
                Read <span className="cell-bold-letters">{averageReadRate}</span>
              </Col>
              <Col lg={6} className={"cell-chart-container"}>
                <UniverseReadWriteMetrics {...this.props} graphIndex={`${universeUUID}-read`} type={"read"}/>
              </Col>
            </Row>
            <Row >
              <Col lg={6} className={"cell-bold-label"}>
                Write <span className="cell-bold-letters">{averageWriteRate}</span>
              </Col>
              <Col lg={6} className={"cell-chart-container"}>
                <UniverseReadWriteMetrics {...this.props} graphIndex={`${universeUUID}-write`} type={"write"}/>
              </Col>
            </Row>
          </Row>
        </Col>
      </div>
    )
  }
}
