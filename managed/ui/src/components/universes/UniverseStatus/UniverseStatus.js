// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import './UniverseStatus.scss';
import {ProgressBar} from 'react-bootstrap';
import { isNonEmptyObject, isNonEmptyArray } from '../../../utils/ObjectUtils';

export default class UniverseStatus extends Component {

  render() {
    const {currentUniverse: {universeDetails, universeUUID}, showLabelText, tasks: {customerTaskList}} = this.props;
    const updateInProgress = universeDetails.updateInProgress;
    const updateSucceeded = universeDetails.updateSucceeded;
    let statusClassName = "";
    let statusText = "";
    const universePendingTask = isNonEmptyArray(customerTaskList) ? customerTaskList.find(function(taskItem) {
      return (taskItem.universeUUID === universeUUID && (taskItem.status === "Running" ||
        taskItem.status === "Initializing") && Number(taskItem.percentComplete) !== 100);
    }) : null;
    let statusDisplay = <span/>;
    if (updateSucceeded) {
      statusClassName = 'good';
      if (showLabelText) {
        statusText = 'Ready';
      }
      statusDisplay = (
        <div><i className="fa fa-check-circle" />
          {statusText && <span>{statusText}</span>}
        </div>
      );
    } else {
      if (updateInProgress && isNonEmptyObject(universePendingTask)) {
        if (showLabelText) {
          statusDisplay = (
            <div className="status-pending">
              <div className="status-pending-display-container">
                <i className="fa fa fa-spinner fa-spin"/>
                <span className="status-pending-name">
                  Pending&hellip;
                  {universePendingTask.percentComplete}%
                </span>
                <span className="status-pending-progress-container">
                  <ProgressBar className={"pending-action-progress"} now={universePendingTask.percentComplete}/>
                </span>
              </div>
            </div>
          );
        } else {
          statusDisplay = <div className={"yb-orange"}><i className={"fa fa fa-spinner fa-spin"}/></div>;
        }
        statusClassName = 'pending';
      } else {
        statusClassName = 'bad';
        if (showLabelText) {
          statusText = 'Error';
        }
        statusDisplay = (
          <div><i className="fa fa-warning" />
            {statusText && <span>{statusText}</span>}
          </div>
        );
      }
    }

    return (
      <div className={'universe-status ' + statusClassName}>
        {statusDisplay}
      </div>
    );
  }
}
