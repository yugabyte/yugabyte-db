// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import {withRouter, browserHistory} from 'react-router';
import {isNonEmptyString, isNonEmptyArray, isNonEmptyObject} from 'utils/ObjectUtils';
import {YBButton} from '../../common/forms/fields';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import './TaskDetail.scss';
import { StepProgressBar } from '../../common/indicators';
import { YBResourceCount, YBCost } from 'components/common/descriptors';
import './TaskDetail.scss';
import moment from 'moment';

class TaskDetail extends Component {
  constructor(props) {
    super(props);
    this.gotoTaskList = this.gotoTaskList.bind(this);
  }
  gotoTaskList() {
    browserHistory.push(this.context.prevPath);
  }
  componentWillMount() {
    const {params} = this.props;
    let currentTaskUUID = params.taskUUID;
    if (isNonEmptyString(currentTaskUUID)) {
      this.props.fetchCurrentTaskDetail(currentTaskUUID);
      this.props.fetchFailedTaskDetail(currentTaskUUID);
    }
  }
  render() {
    const {tasks: {failedTasks, taskProgressData}, params} = this.props;
    let currentTaskData = taskProgressData.data;
    let failedTaskList = isNonEmptyArray(failedTasks.data.failedSubTasks) ? failedTasks.data.failedSubTasks.map(function(subTask){
      return {
        "creationTime": subTask.creationTime,
        "id": subTask.subTaskUUID,
        "taskType": subTask.subTaskType,
        "taskState": subTask.subTaskState,
        "groupType": subTask.subTaskGroupType
      }
    }) : [];
    let formatDateField = function(cell, row) {
      return moment(cell).format("YYYY-MM-DD hh:mm:ss a");
    }
    let taskTopLevelData = <span/>;
    if (isNonEmptyObject(currentTaskData)) {
      let taskTitle = currentTaskData.title.replace(/.*:\s*/, '');
      taskTopLevelData =
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
    }
    let taskProgressBarData = <span/>;
    if (taskProgressData.data.details && isNonEmptyArray(taskProgressData.data.details.taskDetails)) {
      taskProgressBarData = <StepProgressBar progressData={taskProgressData.data}/>;
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
          <BootstrapTable data={failedTaskList}>
          <TableHeaderColumn dataField="id" isKey={true} hidden={true}/>
          <TableHeaderColumn dataField="creationTime" dataFormat={formatDateField}
                             columnClassName="no-border name-column" className="no-border">
            Created On
          </TableHeaderColumn>
          <TableHeaderColumn dataField="taskType" columnClassName="no-border name-column"
                             className="no-border">
            Task Type
          </TableHeaderColumn>
          <TableHeaderColumn dataField="taskState" columnClassName="no-border name-column"
                             className="no-border">
            Task State
          </TableHeaderColumn>
          <TableHeaderColumn dataField="groupType" columnClassName="no-border"
                             className="no-border" dataAlign="left">
            Group Type
          </TableHeaderColumn>
        </BootstrapTable>
        </div>
      </div>
    )
  }
}

TaskDetail.contextTypes = {
  prevPath: PropTypes.string
}

export default withRouter(TaskDetail);
