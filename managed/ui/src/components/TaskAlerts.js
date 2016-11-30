// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import {isValidObject, isValidArray} from '../utils/ObjectUtils';
import { ListGroup, ListGroupItem} from 'react-bootstrap';
import moment from 'moment';

class ShowMoreString extends Component {
  render() {
    const {count} = this.props;
    if (isNaN(count) || count <= 0) {
      return <span/>;
    }
    return (<p>Show { count } more <i className="fa fa-chevron-right"></i></p>);
  }
}

class AlertItem extends Component {
  render() {
    const {taskInfo} = this.props;
    const currentStatus =  taskInfo.success === true ?
                             <i className='fa fa-check-square-o fa-fw'></i>
                               :
                             <i className='fa fa-times fa-fw'></i>;
    var timeStampDifference = moment(taskInfo.createTime).fromNow();
    var [universeName, currentTask] = taskInfo.title.split(":")
    return (
      <ListGroup>
        <ListGroupItem className='task-alerts-cell-container'>
          <div className='task-alerts-cell-head'>
            {currentStatus}
            {currentTask}
            <div className="task-alerts-universe-name">
              {universeName}
            </div>
          </div>
          <div className='task-alerts-cell-body'>
            {timeStampDifference}
          </div>
        </ListGroupItem>
      </ListGroup>
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

    var showMoreString = <ShowMoreString count={taskListLength - numOfElementsToDisplay}/>;
    return (
      <div className="task-alerts-list-container">
        <div className="task-list-heading"><i className="fa fa-bars"></i> Tasks</div>
        {tasksDisplayList}
        <div className="task-alerts-show-more">{showMoreString}</div>
      </div>
    )
  }
}
TaskAlerts.propTypes = {
  eventKey: PropTypes.string.isRequired
}
