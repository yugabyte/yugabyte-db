// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';
import { TaskProgressBar, TaskProgressStepBar } from '..';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { YBLoading } from '../../common/indicators';
import { TASK_SHORT_TIMEOUT } from '../constants';
import { getPromiseState } from '../../../utils/PromiseUtils';

export default class TaskProgress extends Component {
  static contextTypes = {
    router: PropTypes.object
  };

  static propTypes = {
    taskUUIDs: PropTypes.array,
    type: PropTypes.oneOf(['Bar', 'StepBar']),
    timeoutInterval: PropTypes.number
  };

  static defaultProps = {
    type: 'Widget',
    timeoutInterval: TASK_SHORT_TIMEOUT
  };

  componentDidMount() {
    const { taskUUIDs } = this.props;
    if (taskUUIDs && taskUUIDs.length > 0) {
      // TODO, currently we only show one of the tasks, we need to
      // implement a way to show all the tasks against a universe
      this.props.fetchTaskProgress(taskUUIDs[0]);
    }
  }

  componentWillUnmount() {
    clearTimeout(this.timeout);
    this.props.resetTaskProgress();
  }

  componentDidUpdate(prevProps) {
    if (
      this.props.taskProgressData !== prevProps.taskProgressData &&
      !getPromiseState(this.props.taskProgressData).isLoading()
    ) {
      clearTimeout(this.timeout);
      const {
        taskProgressData: { data },
        onTaskSuccess
      } = this.props;
      if (data.status === 'Success' && onTaskSuccess) {
        this.props.onTaskSuccess();
      }
      // Check to make sure if the current state is in not the final state.
      if (
        isDefinedNotNull(data) &&
        (data.status === 'Created' ||
          data.status === 'Abort' ||
          data.status === 'Running' ||
          data.status === 'Initializing')
      ) {
        this.scheduleFetch();
      }
    }
  }

  scheduleFetch() {
    const { taskUUIDs, timeoutInterval } = this.props;
    this.timeout = setTimeout(() => this.props.fetchTaskProgress(taskUUIDs[0]), timeoutInterval);
  }

  render() {
    const { taskUUIDs, taskProgressData, type, featureFlags } = this.props;
    const isNewTaskDetailsUIEnabled = featureFlags?.test?.newTaskDetailsUI || featureFlags?.released?.newTaskDetailsUI;
    if(isNewTaskDetailsUIEnabled) {
      return <span />;
    }
    const taskProgressPromise = getPromiseState(taskProgressData);
    if (taskUUIDs.length === 0) {
      return <span />;
    } else if (taskProgressPromise.isLoading() || taskProgressPromise.isInit()) {
      return <YBLoading />;
    }
    if (type === 'StepBar') {
      return (
        <div className="provider-task-progress-container">
          <TaskProgressStepBar progressData={taskProgressData.data} />
        </div>
      );
    } else {
      return (
        <div className="provider-task-progress-container">
          <TaskProgressBar progressData={taskProgressData.data} />
        </div>
      );
    }
  }
}
