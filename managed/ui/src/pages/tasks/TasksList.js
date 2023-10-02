// Copyright (c) YugaByte, Inc.

import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../../components/common/indicators';
// import { TasksListContainer } from '../../components/tasks';
const TasksListContainer = lazy(() => import('../../components/tasks/TaskList/TasksListContainer'));

export default class TasksList extends Component {
  render() {
    return (
      <div>
        <Suspense fallback={YBLoadingCircleIcon}>
          <TasksListContainer />
        </Suspense>
      </div>
    );
  }
}
