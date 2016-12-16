// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { ListGroup, ListGroupItem } from 'react-bootstrap';
import { Link } from 'react-router';
import { isValidObject, isValidArray } from '../../utils/ObjectUtils';
import moment from 'moment';

import './stylesheets/TaskAlerts.scss'

class AlertItem extends Component {
  render() {
    const {taskInfo} = this.props;
    const statusText = taskInfo.success ? 'completed' : 'failed';
    const statusVerbClassName = taskInfo.success ? 'yb-success-color' : 'yb-fail-color';
    const statusIconClassName = taskInfo.success ? 'fa-check yb-success-color' : 'fa-warning yb-fail-color';
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
    this.props.fetchUniverseTasks();
  }
  componentWillUnMount() {
    this.props.resetUniverseTasks();
  }
  render() {
    const {universe: {universeTasks}} = this.props;
    const numOfElementsToDisplay = 4;
    if(isValidObject(universeTasks)) {
      var tasksList = [];
      Object.keys(universeTasks).forEach(function (key, idx) {
        tasksList = [].concat(tasksList, universeTasks[key]);
      });
      var taskListLength = tasksList.length;
      var displayItems = taskListLength > numOfElementsToDisplay ? tasksList.slice(0,numOfElementsToDisplay) : tasksList
      var tasksDisplayList = [];
      if(isValidArray(tasksList)) {
        tasksDisplayList = displayItems.map(function(listItem, idx){
          return (<AlertItem key={`alertItem${idx}`} taskInfo={listItem}/>)
        });
      }
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
