// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import './stylesheets/StepProgressBar.scss';

export default class StepProgressBar extends Component {
  render() {
    const { details: { taskDetails } } = this.props.progressData;

    var taskClassName = "";
    var getTaskClass = function(type) {
      if ( type === "Initializing" || type === "Unknown") {
        return "pending";
      } else if ( type === "Success" ) {
        return "finished";
      } else if ( type === "Running" ) {
        return "running";
      } else if (type === "Failure") {
        return "failed"
      }
      return null;
    };
    const listLabels = taskDetails.map(function(item, idx) {
      taskClassName = getTaskClass(item.state);
      return (
        <li key={idx} className={taskClassName}>{item.title}</li>
      );
    }, this);
    return (
      <ul className="progressbar">
        {listLabels}
      </ul>
    );
  }
}
