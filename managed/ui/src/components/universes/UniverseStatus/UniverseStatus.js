// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import './UniverseStatus.scss';

export default class UniverseStatus extends Component {
  render() {
    const { universe: {universeDetails}, showLabelText } = this.props;
    var updateInProgress = universeDetails.updateInProgress;
    var updateSucceeded = universeDetails.updateSucceeded;
    var statusIcon = <span/>;
    var statusClassName = "";
    var statusText = "";
    if (updateSucceeded) {
      statusIcon = <i className="fa fa-check-circle" />;
      statusClassName = 'good';
      if (showLabelText) {
        statusText = 'Ready';
      }
    } else {
      if (updateInProgress) {
        statusIcon = <i className="fa fa-spinner fa-spin" />;
        statusClassName = 'pending';
        if (showLabelText) {
          statusText = 'Pending';
        }
      } else {
        statusIcon  = <i className="fa fa-times-circle" />;
        statusClassName = 'error';
        if (showLabelText) {
          statusText = 'Error';
        }
      }
    }



    return (
      <div className={'universe-status ' + statusClassName}>

        {statusIcon}
        {statusText && <span>{statusText}</span>}
      </div>
    );
  }
}
