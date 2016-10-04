// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import {isValidObject, isValidArray} from '../utils/ObjectUtils';
import {MenuItem} from 'react-bootstrap';
import {FormattedNumber} from 'react-intl';

class AlertItem extends Component {
  render() {
    const {taskHeading, status, percentComplete, eventKey} = this.props;
    const percentFinished =  status === true ?
                             <span><FormattedNumber value={percentComplete/100} style={"percent"}/>
                               Finished<i className='fa fa-check-square-o fa-fw'></i></span>
                               :
                             <span><FormattedNumber value={percentComplete/100} style={"percent"}/>
                               Finished<i className='fa fa-times fa-fw'></i></span>;

    return (
      <MenuItem eventKey={eventKey} className='task-alerts-cell-container'>
        <div className='task-alerts-cell-head'>
            {taskHeading}
          </div>
          <div className='task-alerts-cell-body'>
            {percentFinished}
          </div>
      </MenuItem>
    )
  }
}
AlertItem.propTypes = {
  taskHeading: PropTypes.string.isRequired,
  status: PropTypes.bool.isRequired,
  percentComplete: PropTypes.number.isRequired,
  eventKey: PropTypes.string.isRequired
}

export default class TaskAlerts extends Component {
  componentDidMount() {
    this.props.fetchUniverseTasks();
  }
  componentWillUnMount() {
    this.props.resetUniverseTasks();
  }

  render() {
    const {universe:{universeTasks}, eventKey} = this.props;
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
          var childEventKey = `${eventKey}.${idx}`;
          return (<AlertItem key={`alertItem${idx}`}
                  taskHeading={listItem.title}
                  percentComplete={listItem.percentComplete}
                  status={listItem.success} eventKey={childEventKey}/>)
        });
      }
    }
    var showMoreString = taskListLength > numOfElementsToDisplay ? "show " +
                         ( taskListLength - numOfElementsToDisplay ) + " more" : "";
    return (
      <div className="task-alerts-list-container">
        {tasksDisplayList}
        <div className="task-alerts-show-more">{showMoreString}</div>
      </div>
    )
  }
}
TaskAlerts.propTypes = {
  eventKey: PropTypes.string.isRequired
}
