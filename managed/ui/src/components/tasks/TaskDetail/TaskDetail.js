// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';
import { browserHistory, Link, withRouter } from 'react-router';
import { isNonEmptyArray, isNonEmptyObject, isNonEmptyString } from '../../../utils/ObjectUtils';
import './TaskDetail.scss';
import { StepProgressBar } from '../../common/indicators';
import { YBResourceCount } from '../../common/descriptors';
import { Col, Row } from 'react-bootstrap';
import { YBPanelItem } from '../../panels';
import _ from 'lodash';
import { Highlighter } from '../../../helpers/Highlighter';
import { getPromiseState } from '../../../utils/PromiseUtils';
import 'highlight.js/styles/github.css';
import { toast } from 'react-toastify';
import { timeFormatter } from '../../../utils/TableFormatters';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { calculateDuration } from '../../backupv2/common/BackupUtils';
import { SoftwareUpgradeTaskType } from '../../universes/helpers/universeHelpers';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';

class TaskDetail extends Component {
  constructor(props) {
    super(props);
    this.state = { errorStringDisplay: false };
  }

  gotoTaskList = () => {
    browserHistory.push(this.context.prevPath);
  };

  toggleErrorStringDisplay = () => {
    this.setState({ errorStringDisplay: !this.state.errorStringDisplay });
  };

  retryTaskClicked = (currentTaskUUID) => {
    this.props.retryCurrentTask(currentTaskUUID).then((response) => {
      const status = response?.payload?.response?.status || response?.payload?.status;
      if (status === 200 || status === 201) {
        browserHistory.push('/tasks');
      } else {
        const taskResponse = response?.payload?.response;
        const toastMessage = taskResponse?.data?.error
          ? taskResponse?.data?.error
          : taskResponse?.statusText;
        toast.error(toastMessage);
      }
    });
  };

  rollbackTaskClicked = (currentTaskUUID) => {
    this.props.rollbackCurrentTask(currentTaskUUID).then((response) => {
      const status = response?.payload?.response?.status || response?.payload?.status;
      if (status === 200 || status === 201) {
        browserHistory.push('/tasks');
      } else {
        const taskResponse = response?.payload?.response;
        const toastMessage = taskResponse?.data?.error
          ? taskResponse?.data?.error
          : taskResponse?.statusText;
        toast.error(toastMessage);
      }
    });
  };

  componentDidMount() {
    const { params, fetchCurrentTaskDetail, fetchFailedTaskDetail, fetchUniverseList } = this.props;
    const currentTaskUUID = params.taskUUID;
    if (isNonEmptyString(currentTaskUUID)) {
      fetchCurrentTaskDetail(currentTaskUUID);
      fetchFailedTaskDetail(currentTaskUUID);
    }
    fetchUniverseList();
  }

  render() {
    const {
      tasks: { failedTasks, taskProgressData },
      params: { taskUUID },
      universe: { universeUUID }
    } = this.props;
    const self = this;
    const currentTaskData = taskProgressData.data;
    let taskTopLevelData = <span />;
    if (isNonEmptyObject(currentTaskData)) {
      taskTopLevelData = (
        <div className={'task-detail-status'}>
          <div className="pull-right">{Math.round(currentTaskData.percent)}% complete</div>
          <div className={currentTaskData.status.toLowerCase()}>{currentTaskData.status}</div>
        </div>
      );
    }

    let taskProgressBarData = <span />;
    if (
      taskProgressData.data.details &&
      isNonEmptyArray(taskProgressData.data.details.taskDetails)
    ) {
      taskProgressBarData = (
        <StepProgressBar progressData={taskProgressData.data} status={currentTaskData.status} />
      );
    }
    let taskFailureDetails = <span />;
    const getTruncatedErrorString = (errorString) => {
      const truncatedError = _.truncate(errorString, {
        length: 400,
        separator: /,? +/
      });
      return <Highlighter type="json" text={truncatedError} element="pre" />;
    };

    const getErrorMessageDisplay = (errorString, taskUUID) => {
      let errorElement = getTruncatedErrorString(errorString);
      let displayMessage = 'Expand';
      let displayIcon = <i className="fa fa-expand"></i>;
      if (self.state.errorStringDisplay) {
        errorElement = <Highlighter type="json" text={errorString} element="pre" />;
        displayMessage = 'View Less';
        displayIcon = <i className="fa fa-compress"></i>;
      }
      return (
        <div className="clearfix">
          <div className="onprem-config__json">{errorElement}</div>
          <div
            className="btn btn-orange text-center pull-right task-detail-button"
            onClick={self.toggleErrorStringDisplay}
          >
            {displayIcon}
            {displayMessage}
          </div>
          {/* TODO use API response to check retryable. */}
          {isNonEmptyString(currentTaskData.title) && currentTaskData.retryable && (
            <RbacValidator
              accessRequiredOn={{
                onResource: currentTaskData?.targetUUID,
                ...ApiPermissionMap.RETRY_TASKS
              }}
              isControl
              overrideStyle={{ float: 'right' }}
            >
              <div
                className="btn btn-orange text-center pull-right task-detail-button"
                onClick={() => self.retryTaskClicked(taskUUID)}
              >
                <i className="fa fa-refresh"></i>
                Retry Task
              </div>
            </RbacValidator>
          )}
          {isNonEmptyString(currentTaskData.title) && currentTaskData.canRollback && (
            <RbacValidator
              accessRequiredOn={{
                onResource: currentTaskData?.targetUUID,
                ...ApiPermissionMap.RETRY_TASKS
              }}
              isControl
              overrideStyle={{ float: 'right' }}
            >
              <div
                className="btn btn-orange text-center pull-right task-detail-button"
                onClick={() => self.rollbackTaskClicked(taskUUID)}
              >
                <i className="fa fa-refresh"></i>
                Rollback Task
              </div>
            </RbacValidator>
          )}
        </div>
      );
    };
    let universe = null;
    if (
      currentTaskData.targetUUID &&
      getPromiseState(this.props.universe.universeList).isSuccess()
    ) {
      const universes = this.props.universe.universeList.data;
      universe = _.find(
        universes,
        (universe) => universe.universeUUID === currentTaskData.targetUUID
      );
    }

    if (isNonEmptyArray(failedTasks.data.failedSubTasks)) {
      taskFailureDetails = failedTasks.data.failedSubTasks.map((subTask) => {
        let errorString = <span />;
        // Show retry only for the last failed task.
        if (subTask.subTaskState === 'Failure' || subTask.subTaskState === 'Aborted') {
          if (subTask.errorString === 'null') {
            subTask.errorString = 'Task failed';
          }
          errorString = getErrorMessageDisplay(subTask.errorString, taskUUID);
        }
        return (
          <div className="task-detail-info" key={subTask.creationTime}>
            <Row>
              <Col xs={4}>
                {subTask.subTaskGroupType}
                <i className="fa fa-angle-right" />
                {subTask.subTaskType}
              </Col>
              <Col xs={4}>{timeFormatter(subTask.creationTime)}</Col>
              <Col xs={4}>{subTask.subTaskState}</Col>
            </Row>
            {errorString}
          </div>
        );
      });
    }

    let heading;
    if (universe) {
      heading = (
        <h2 className="content-title">
          <Link to={`/universes/${universe.universeUUID}`}>{universe.name}</Link>
          <span>
            <i className="fa fa-chevron-right"></i>
            <Link to={`/universes/${universe.universeUUID}/tasks`}>Tasks</Link>
          </span>
        </h2>
      );
    } else {
      heading = (
        <h2 className="content-title">
          <Link to="/tasks/">Tasks</Link>
          <span>
            <i className="fa fa-chevron-right"></i>
            {currentTaskData?.title || 'Task Details'}
          </span>
        </h2>
      );
    }

    return (
      <div className="task-container">
        {heading}
        <div className="task-detail-overview">
          <div className="task-top-heading">
            <YBResourceCount
              className="text-align-right pull-right"
              kind="Target universe"
              sizeClassName="task-header-font"
              size={currentTaskData?.title?.split(' : ')[1]}
            />
            <YBResourceCount
              kind="Task name"
              sizeClassName="task-header-font"
              size={currentTaskData?.title?.split(' : ')[0]}
            />
            {taskTopLevelData}
          </div>
          <div className="task-step-bar-container">{taskProgressBarData}</div>
        </div>
        <YBPanelItem
          header={
            <h2>
              {currentTaskData.correlationId ? (
                <Link to={`/logs/?queryRegex=${currentTaskData.correlationId}`}>Task details</Link>
              ) : (
                'Task details'
              )}
            </h2>
          }
          body={
            [
              SoftwareUpgradeTaskType.SOFTWARE_UPGRADE,
              SoftwareUpgradeTaskType.ROLLBACK_UPGRADE,
              SoftwareUpgradeTaskType.FINALIZE_UPGRADE
            ].includes(currentTaskData?.type) ? (
              <div className="task-detail-container">
                <Row className="task-heading-row">
                  <Col xs={3}>TASK</Col>
                  <Col xs={2}>START VERSION</Col>
                  <Col xs={2}>TARGET VERSION</Col>
                  <Col xs={3}>START TIME</Col>
                  <Col xs={2}>DURATION</Col>
                </Row>
                <Row className="task-value-row">
                  <Col xs={3}>
                    <>
                      {currentTaskData?.type === SoftwareUpgradeTaskType.SOFTWARE_UPGRADE &&
                        'Database Upgrade'}
                      {currentTaskData?.type === SoftwareUpgradeTaskType.ROLLBACK_UPGRADE &&
                        'Roll Back Database Upgrade'}
                      {currentTaskData?.type === SoftwareUpgradeTaskType.FINALIZE_UPGRADE &&
                        'Finalize Database Upgrade'}
                    </>
                  </Col>
                  <Col xs={2}>
                    {currentTaskData?.details?.versionNumbers?.ybPrevSoftwareVersion ?? '-'}
                  </Col>
                  <Col xs={2}>
                    {currentTaskData?.details?.versionNumbers?.ybSoftwareVersion ?? '-'}
                  </Col>
                  <Col xs={3}>
                    {currentTaskData?.createTime ? ybFormatDate(currentTaskData?.createTime) : '-'}
                  </Col>
                  <Col xs={2}>
                    {currentTaskData?.createTime && currentTaskData?.completionTime
                      ? calculateDuration(
                          currentTaskData?.createTime,
                          currentTaskData?.completionTime
                        )
                      : '-'}
                  </Col>
                </Row>
                {taskFailureDetails}
              </div>
            ) : (
              <div className="task-detail-container">
                <Row className="task-heading-row">
                  <Col xs={4}>Task</Col>
                  <Col xs={4}>Started On</Col>
                  <Col xs={4}>Status</Col>
                </Row>
                {taskFailureDetails}
              </div>
            )
          }
        />
      </div>
    );
  }
}

TaskDetail.contextTypes = {
  prevPath: PropTypes.string
};

export default withRouter(TaskDetail);
