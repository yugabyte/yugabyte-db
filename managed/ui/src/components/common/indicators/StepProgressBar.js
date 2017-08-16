// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import './stylesheets/StepProgressBar.css'

export default class StepProgressBar extends Component {
  render() {
    const { details: { taskDetails } } = this.props.progressData;
    var taskClassName = "";
    var getTaskClass = function(type) {
      if ( type === "Initializing" ) {
        return "pending";
      } else if ( type === "Success" ) {
        return "finished";
      } else if ( type === "Running" ) {
        return "running";
      }
      return null;
    }
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
    )
  }
}
