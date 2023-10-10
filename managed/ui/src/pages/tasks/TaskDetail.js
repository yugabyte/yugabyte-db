// Copyright (c) YugaByte, Inc.

import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../../components/common/indicators';
// import { TaskDetailContainer } from '../../components/tasks';

const TaskDetailContainer = lazy(() =>
  import('../../components/tasks/TaskDetail/TaskDetailContainer')
);

export default class TaskDetail extends Component {
  render() {
    return (
      <div>
        <Suspense fallback={YBLoadingCircleIcon}>
          <TaskDetailContainer />
        </Suspense>
      </div>
    );
  }
}
