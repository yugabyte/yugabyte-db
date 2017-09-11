// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import {withRouter, browserHistory} from 'react-router';
import {isNonEmptyString, isNonEmptyArray, isNonEmptyObject} from 'utils/ObjectUtils';
import './TaskDetail.scss';
import { StepProgressBar } from '../../common/indicators';
import { YBResourceCount } from 'components/common/descriptors';
import {Row, Col} from 'react-bootstrap';
import './TaskDetail.scss';
import moment from 'moment';
import _ from 'lodash';
import Highlight from 'react-highlight';
import "highlight.js/styles/github.css";

class TaskDetail extends Component {
  constructor(props) {
    super(props);
    this.gotoTaskList = this.gotoTaskList.bind(this);
    this.toggleErrorStringDisplay = this.toggleErrorStringDisplay.bind(this);
    this.state = {errorStringDisplay: false};
  };
  gotoTaskList() {
    browserHistory.push(this.context.prevPath);
  };
  toggleErrorStringDisplay() {
    this.setState({errorStringDisplay: !this.state.errorStringDisplay});
  };
  componentWillMount() {
    const {params} = this.props;
    const currentTaskUUID = params.taskUUID;
    if (isNonEmptyString(currentTaskUUID)) {
      this.props.fetchCurrentTaskDetail(currentTaskUUID);
      this.props.fetchFailedTaskDetail(currentTaskUUID);
    }
  }
  render() {
    const {tasks: {failedTasks, taskProgressData}} = this.props;
    const self = this;
    const currentTaskData = taskProgressData.data;
    const formatDateField = function(cell) {
      return moment(cell).format("YYYY-MM-DD hh:mm:ss a");
    };
    let taskTopLevelData = <span/>;
    if (isNonEmptyObject(currentTaskData)) {
      const taskTitle = currentTaskData.title.replace(/.*:\s*/, '');
      taskTopLevelData =(
        <div className={"universe-resources"}>
          <div className="task-detail-back-button" onClick={this.gotoTaskList}><i className="fa fa-chevron-left"/>&nbsp;Back</div>
          <div className="task-meta-container">
            <YBResourceCount kind="Title" size={taskTitle}/>
            <YBResourceCount kind="Status" size={currentTaskData.status}/>
            <YBResourceCount kind="Target" size={currentTaskData.target}/>
            <YBResourceCount kind="Type" size={currentTaskData.type}/>
            <YBResourceCount kind="Percent Complete" size={currentTaskData.percent} unit={"%"}/>
          </div>
        </div>
      );
    };

    let taskProgressBarData = <span/>;
    if (taskProgressData.data.details && isNonEmptyArray(taskProgressData.data.details.taskDetails)) {
      taskProgressBarData = <StepProgressBar progressData={taskProgressData.data}/>;
    }
    let taskFailureDetails = <span/>;
    const getTruncatedErrorString = function(errorString) {
      return (
        <Highlight className='json'>
          {_.trunc(errorString, {
            'length': 400,
            'separator': /,? +/
          })}
        </Highlight>
      );
    };

    const getErrorMessageDisplay = function(errorString) {
      let errorElement = getTruncatedErrorString(errorString);
      let chevronClassName = "fa fa-chevron-down";
      let displayMessage = "View More";
      if (self.state.errorStringDisplay) {
        errorElement = <Highlight className='json'>{errorString}</Highlight>;
        chevronClassName = "fa fa-chevron-up";
        displayMessage = "View Less";
      }

      return (
        <div>
          {errorElement}
          <div className="task-detail-view-toggle text-center" onClick={self.toggleErrorStringDisplay}>
            {displayMessage}&nbsp;<i className={chevronClassName} />
          </div>
        </div>
      );
    };

    if (isNonEmptyArray(failedTasks.data.failedSubTasks)) {
      taskFailureDetails = failedTasks.data.failedSubTasks.map(function(subTask){
        let errorString = <span/>;
        if (subTask.errorString !== "null") {
          errorString =
            (
              <Col lg={12} className="error-string-detail-container">
                {getErrorMessageDisplay(subTask.errorString)}
              </Col>
            );
        }
        return (
          <div key={subTask.creationTime}>
            <Col lg={3}>{formatDateField(subTask.creationTime)}</Col>
            <Col lg={3}>{subTask.subTaskType}</Col>
            <Col lg={3}>{subTask.subTaskState}</Col>
            <Col lg={3}>{subTask.subTaskGroupType}</Col>
            {errorString}
          </div>
        );
      });
    }

    return (
      <div className="task-failure-container">
        <div className="task-failure-top-heading">
          {taskTopLevelData}
        </div>
        <div className="task-step-bar-container">
          {taskProgressBarData}
        </div>
        <div className="task-failure-detail-heading">Task Failure Details</div>
        <div className="task-failure-detail-container">
          <Row className="task-failure-heading-row">
            <Col lg={3}>Created On</Col>
            <Col lg={3}>Task Type</Col>
            <Col lg={3}>Task State</Col>
            <Col lg={3}>Group Type</Col>
          </Row>
          <Row>
            {taskFailureDetails}
          </Row>
        </div>
      </div>
    );
  }
}

TaskDetail.contextTypes = {
  prevPath: PropTypes.string
};

export default withRouter(TaskDetail);
