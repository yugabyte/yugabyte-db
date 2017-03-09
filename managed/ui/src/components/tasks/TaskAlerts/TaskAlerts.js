// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Link } from 'react-router';
import { isValidArray } from '../../../utils/ObjectUtils';
import moment from 'moment';

import './TaskAlerts.scss'

class AlertItem extends Component {
  render() {
    const {taskInfo} = this.props;
    var statusText = "";
    var statusVerbClassName = "";
    var statusIconClassName = "";
    if (taskInfo.status === "Initializing") {
      statusText = 'initializing';
      statusVerbClassName = "yb-pending-color";
      statusIconClassName = "fa-spinner fa-spin yb-pending-color";
    } else if (taskInfo.status === "Running") {
      statusText = 'pending';
      statusVerbClassName = "yb-pending-color";
      statusIconClassName = "fa-spinner fa-spin yb-pending-color";
    } else if (taskInfo.status === "Success") {
      statusText = 'completed';
      statusVerbClassName = "yb-success-color";
      statusIconClassName = "fa-check yb-success-color";
    } else if (taskInfo.status === "Failure"){
      statusText = 'failed';
      statusVerbClassName = "yb-fail-color";
      statusIconClassName = "fa-warning yb-fail-color";
    } else {
      statusText = 'unknown';
      statusVerbClassName = "yb-unknown-color";
      statusIconClassName = "fa-exclamation yb-unknown-color";
    }

    var timeStampDifference = moment(taskInfo.createTime).fromNow();
    var [currentTask, universeName] = taskInfo.title.split(":")
    return (
      <div className='task-cell'>
        <div className='icon icon-hang-left'>
          <i className={`fa ${statusIconClassName}`}></i>
        </div>
        <div className='task-name'>
          {currentTask} <span className="task-universe-name">{universeName}</span>
        </div>
        <div className='task-status'>
          <span className={'task-status-verb ' + statusVerbClassName}>{statusText}</span>
          {timeStampDifference}
        </div>
      </div>
    )
  }
}
AlertItem.propTypes = {
  taskInfo: PropTypes.object.isRequired
}

export default class TaskAlerts extends Component {
  componentDidMount() {
    this.props.fetchCustomerTasks();
  }
  componentWillUnMount() {
    this.props.resetCustomerTasks();
  }
  render() {
    const {tasks: {customerTaskList}} = this.props;
    var tasksDisplayList = [];
    if(isValidArray(customerTaskList)) {
      var displayItems = customerTaskList.slice(0, 4);
      tasksDisplayList = displayItems.map(function(listItem, idx){
        return (<AlertItem key={`alertItem${idx}`} taskInfo={listItem}/>)
      });
    }

    return (
      <div className="task-list-container">
        <div className="task-list-heading">
          <div className="icon icon-hang-left">
            <i className="fa fa-list"></i>
          </div>
          Tasks
        </div>
        {tasksDisplayList}
        <Link to="tasks" className="task-cell">Open Tasks</Link>
      </div>
    )
  }
}
TaskAlerts.propTypes = {
  eventKey: PropTypes.string.isRequired
}
