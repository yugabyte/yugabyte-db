// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {TaskListTable} from '../../tasks';

export default class TasksList extends Component {

  componentWillMount() {
    this.props.fetchCustomerTasks();
  }

  render() {
    const {tasks: {customerTaskList}} = this.props;
    return (
      <TaskListTable taskList={customerTaskList}/>
    );
  }
}
