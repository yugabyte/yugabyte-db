// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import TasksListContainer from '../containers/tasks/TasksListContainer';

export default class Tasks extends Component {
  render() {
    return (
      <div>
        <TasksListContainer/>
      </div>
    )
  }
}
