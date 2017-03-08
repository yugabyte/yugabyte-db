// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, ListGroup, ListGroupItem, ProgressBar } from 'react-bootstrap';
import { Link } from 'react-router';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { isValidObject, isValidArray } from '../../../utils/ObjectUtils';
import './UniverseTable.scss';
import {UniverseReadWriteMetrics} from '../../metrics';
import {YBCost} from '../../common/descriptors';
import {YBLoadingIcon} from '../../common/indicators';

export default class UniverseTable extends Component {

  componentWillMount() {
    this.props.fetchUniverseList();
    this.props.fetchUniverseTasks();
    this.props.universeReadWriteData();
  }

  componentWillUnmount() {
    this.props.resetUniverseTasks();
    this.props.resetTaskProgress();
  }

  render() {
    var self = this;
    const { universe: { universeList, universeTasks, loading }, universeReadWriteData, tasks } = this.props;
    if (loading) {
      return <YBLoadingIcon/>;
    }

    var universeRowItem =
      universeList.map(function (item, idx) {
        var universeTaskUUIDs = [];
        if (isValidArray(tasks.customerTaskList)) {
          universeTaskUUIDs = tasks.customerTaskList.map(function(taskItem){
            if (taskItem.universeUUID === item.universeUUID) {
              return {"id": taskItem.id, "data": taskItem, "universe": item.universeUUID};
            } else {
              return null;
            }
          }).filter(Boolean).sort(function(a, b){
            return a.data.createTime < b.data.createTime;
          })
          console.log(universeTaskUUIDs);
        }
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
  componentWillMount() {
    const {taskId} = this.props;
    this.props.fetchCurrentTaskList(taskId);
  }
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
                <UniverseReadWriteMetrics {...this.props} graphIndex={`${universeUUID}-read`} type={"read"} />
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
