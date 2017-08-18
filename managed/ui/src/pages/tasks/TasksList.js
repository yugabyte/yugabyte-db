// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import TasksListContainer from '../../components/tasks/TaskList/TasksListContainer';

export default class TasksList extends Component {
  render() {
    return (
      <div>
        <TasksListContainer/>
      </div>
    )
  }
}
