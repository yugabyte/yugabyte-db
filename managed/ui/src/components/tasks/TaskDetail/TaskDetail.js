// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Link, withRouter, browserHistory } from 'react-router';
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
    this.state = {errorStringDisplay: false};
  }

  gotoTaskList = () => {
    browserHistory.push(this.context.prevPath);
  };

  toggleErrorStringDisplay = () => {
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
      taskTopLevelData = (
        <div className={"universe-resources"}>
          <YBResourceCount kind="Complete" size={currentTaskData.percent} unit={"%"}/>
          <YBResourceCount kind="Status" size={currentTaskData.status}/>
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

    const getErrorMessageDisplay = errorString => {
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
      taskFailureDetails = failedTasks.data.failedSubTasks.map(subTask => {
        let errorString = <span/>;
        if (subTask.errorString !== "null") {
          errorString = getErrorMessageDisplay(subTask.errorString);
        }
        return (
          <div className="task-detail-info" key={subTask.creationTime}>
            <Row>
              <Col lg={2}>
                {subTask.subTaskGroupType}
                <i className="fa fa-angle-right" />
                {subTask.subTaskType}
              </Col>
              <Col lg={2}>{formatDateField(subTask.creationTime)}</Col>
              <Col lg={2}>{subTask.subTaskState}</Col>
              <Col lg={6}>{errorString}</Col>
            </Row>
          </div>
        );
      });
    }

    let universe = null;
    if (currentTaskData.targetUUID) {
      const universes = (this.props.universe && this.props.universe.universeList &&
        this.props.universe.universeList.data) || [];
      universe = _.find(universes, universe => universe.universeUUID === currentTaskData.targetUUID);
    }

    let heading;
    if (universe) {
      heading = (
        <h2 className="content-title">
          <Link to={`/universes/${universe.universeUUID}`}>
            {universe.name}
          </Link>
          <span>
            <i className="fa fa-chevron-right"></i>
            <Link to={`/universes/${universe.universeUUID}?tab=tasks`}>
              Tasks
            </Link>
            <i className="fa fa-chevron-right"></i>
            {(currentTaskData && currentTaskData.title) || 'Task Details'}
          </span>
        </h2>
      );
    } else {
      heading = (
        <h2 className="content-title">
          <Link to="/tasks/">Tasks</Link>
          <span>
            <i className="fa fa-chevron-right"></i>
            {(currentTaskData && currentTaskData.title) || 'Task Details'}
          </span>
        </h2>
      );
    }

    return (
      <div className="task-failure-container">
        {heading}
        <div className="task-detail-overview">
          <div className="task-failure-top-heading">
            {taskTopLevelData}
          </div>
          <div className="task-step-bar-container">
            {taskProgressBarData}
          </div>
        </div>
        <div className="task-failure-detail-container">
          <Row className="task-failure-heading-row">
            <Col lg={2}>Task</Col>
            <Col lg={2}>Started On</Col>
            <Col lg={2}>Status</Col>
            <Col lg={6}>Details</Col>
          </Row>
          {taskFailureDetails}
        </div>
      </div>
    );
  }
}

TaskDetail.contextTypes = {
  prevPath: PropTypes.string
};

export default withRouter(TaskDetail);
