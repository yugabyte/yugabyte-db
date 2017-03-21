// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import './UniverseStatus.scss';
import {Row, Col, ProgressBar} from 'react-bootstrap';
import {isValidArray, isValidObject} from '../../../utils/ObjectUtils';

export default class UniverseStatus extends Component {

  render() {
    const {currentUniverse: {universeDetails, universeUUID}, showLabelText, tasks: {customerTaskList}} = this.props;
    var updateInProgress = universeDetails.updateInProgress;
    var updateSucceeded = universeDetails.updateSucceeded;
    var statusClassName = "";
    var statusText = "";
    var universePendingTask = customerTaskList.find(function(taskItem){
      return (taskItem.universeUUID === universeUUID && (taskItem.status === "Running"
          ||  taskItem.status === "Initializing") && Number(taskItem.percentComplete) !== 100);
    });
    var statusDisplay = <span/>;
    if (updateSucceeded) {
      statusClassName = 'good';
      if (showLabelText) {
        statusText = 'Ready';
      }
      statusDisplay =
        <div> <i className="fa fa-check-circle" />
          {statusText && <span>{statusText}</span>}
        </div>
    } else {
      if (updateInProgress && isValidObject(universePendingTask) && isValidArray(Object.keys(universePendingTask))) {
        if (showLabelText) {
          statusDisplay =
            <div className={"status-pending"}>
              <Row className={"status-pending-display-container"}>
                <span className={"status-pending-name"}>{universePendingTask.percentComplete}% complete&nbsp;</span>
                <i className={"fa fa fa-spinner fa-spin"}/>
                <Col className={"status-pending-name"}>
                  Pending...
                </Col>
              </Row>
              <Row className={"status-pending-progress-container "}>
                <ProgressBar className={"pending-action-progress"} now={universePendingTask.percentComplete}/>
              </Row>
            </div>
        } else {
           statusDisplay = <div className={"yb-orange"}><i className={"fa fa fa-spinner fa-spin"}/></div>;
        }
        statusClassName = 'pending';
      } else {
        statusClassName = 'bad';
        if (showLabelText) {
          statusText = 'Error';
        }
        statusDisplay =
          <div> <i className="fa fa-warning" />
            {statusText && <span>{statusText}</span>}
          </div>
      }
    }

    return (
      <div className={'universe-status ' + statusClassName}>
        {statusDisplay}
      </div>
    );
  }
}
