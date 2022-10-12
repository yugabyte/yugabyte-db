// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import './stylesheets/StepProgressBar.scss';

export default class StepProgressBar extends Component {
  isFailedIndex = (taskDetails) => {
    return taskDetails.findIndex((element) => {
      return element.state === 'Failure' || element.state === 'Aborted';
    });
  };

  isRunningIndex = (taskDetails) => {
    return taskDetails.findIndex((element) => {
      return element.state === 'Running';
    });
  };

  normalizeTasks = (taskDetails , taskStatus) => {
    const taskDetailsNormalized = [
      ...taskDetails,
      {
        description: 'Universe created',
        state: 'Unknown',
        title: 'Done'
      }
    ];
    // if the task is failed and all sub task is in unknown state, then show failure.
    if(taskStatus === 'Failure' && this.isFailedIndex(taskDetailsNormalized) === -1){
      taskDetailsNormalized[taskDetailsNormalized.length -1]['state'] = 'Failure';
    }

    if (this.isFailedIndex(taskDetailsNormalized) > -1) {
      for (let i = 0; i < this.isFailedIndex(taskDetailsNormalized); i++) {
        taskDetailsNormalized[i].class = 'to-be-failed';
      }
    } else if (this.isRunningIndex(taskDetailsNormalized) > -1) {
      for (let i = 0; i < this.isRunningIndex(taskDetailsNormalized); i++) {
        taskDetailsNormalized[i].class = 'to-be-succeed';
      }
    } else if (taskStatus !== 'Created' && taskStatus !== 'Abort' && taskStatus !=='Running') {
      taskDetailsNormalized.forEachclass = 'finished';
      taskDetailsNormalized[taskDetailsNormalized.length - 1].class = 'finished';
    }
    return taskDetailsNormalized;
  };

  render() {
    const {
      details: { taskDetails },
      status
    } = this.props.progressData;
    let taskClassName = '';
    const getTaskClass = function (type) {
      if (type === 'Initializing' || type === 'Unknown') {
        return 'pending';
      } else if (type === 'Success') {
        return 'finished';
      } else if (type === 'Running') {
        return 'running';
      } else if (type === 'Failure') {
        return 'failed';
      } else if (type === 'Aborted') {
        return 'failed';
      }
      return null;
    };
    const taskDetailsNormalized = this.normalizeTasks(taskDetails, status);

    const tasksTotal = taskDetailsNormalized.length - 1;
    const taskIndex = taskDetailsNormalized.findIndex((element) => {
      return (
        element.state === 'Running' || element.state === 'Failure' || element.state === 'Aborted'
      );
    });
    const progressbarClass =
        (status === 'Failure' || status === 'Aborted')
        ? 'failed'
        : (status === 'Created' || status === 'Abort' || status === 'Running')
        ? 'running'
        : 'finished';
    const barWidth =
        tasksTotal === 0
        ? ((status !== 'Success')? '0%' : '100%')
        : (100 * (taskIndex + (this.isFailedIndex(taskDetailsNormalized) > -1 ? 0 : 0.5))) /
            tasksTotal + '%';

    const listLabels = taskDetailsNormalized.map(function (item, idx) {
      taskClassName = getTaskClass(item.state);
      return (
        <li key={idx} className={taskClassName + ' ' + item.class}>
          <span>{item.title}</span>
        </li>
      );
    }, this);
    return (
      <ul className="progressbar">
        <div
          className={
            'progressbar-bar ' +
            progressbarClass +
            ' ' +
            (taskIndex > -1 ? getTaskClass(taskDetailsNormalized[taskIndex].state) : '')
          }
          style={{ width: barWidth }}
        ></div>
        {listLabels}
      </ul>
    );
  }
}
