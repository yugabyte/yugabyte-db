// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { TaskProgressBar, TaskProgressStepBar  } from '..';
import { isValidObject } from '../../../utils/ObjectUtils';
import { YBLoadingIcon } from '../../common/indicators';
import { getPromiseState } from '../../../utils/PromiseUtils';

export default class TaskProgress extends Component {
  static contextTypes = {
    router: PropTypes.object
  }

  static propTypes = {
    taskUUIDs: PropTypes.array,
    type: PropTypes.oneOf(['Bar', 'StepBar'])
  }

  static defaultProps = {
    type: 'Widget'
  }

  componentDidMount() {
    const { taskUUIDs } = this.props;
    if (taskUUIDs && taskUUIDs.length > 0) {
      // TODO, currently we only show one of the tasks, we need to
      // implement a way to show all the tasks against a universe
      this.props.fetchTaskProgress(taskUUIDs[0]);
    }
  }

  componentWillUnmount() {
    this.props.resetTaskProgress();
  }

  componentWillReceiveProps(nextProps) {
    if (this.props.taskProgressData !== nextProps.taskProgressData &&
          !getPromiseState(nextProps.taskProgressData).isLoading()) {
      clearTimeout(this.timeout);

      const { taskProgressData: { data }} = nextProps;
        // Check to make sure if the current state is running
      if (isValidObject(data) && (data.status === "Running" || data.status === "Initializing")) {
        this.scheduleFetch();
      }
    }
  }

  scheduleFetch() {
    const { taskUUIDs } = this.props;
    this.timeout = setTimeout(() => this.props.fetchTaskProgress(taskUUIDs[0]), 60000);
  }

  render() {
    const { taskUUIDs, taskProgressData, type } = this.props;
    const taskProgressPromise = getPromiseState(taskProgressData);
    if (taskUUIDs.length === 0) {
      return <span />;
    } else if (taskProgressPromise.isLoading() || taskProgressPromise.isInit()) {
      return <YBLoadingIcon/>;
    }
    if (type === "StepBar") {
      return <TaskProgressStepBar progressData={taskProgressData.data}/>;
    } else {
      return <TaskProgressBar progressData={taskProgressData.data} />;
    }
  }
}
