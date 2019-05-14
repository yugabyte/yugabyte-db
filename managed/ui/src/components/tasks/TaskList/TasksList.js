// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { TaskListTable } from '../../tasks';
import { showOrRedirect } from 'utils/LayoutUtils';

export default class TasksList extends Component {

  componentWillMount() {
    this.props.fetchCustomerTasks();
  }

  render() {
    const {tasks: {customerTaskList}, customer: { currentCustomer }} = this.props;
    showOrRedirect(currentCustomer.data.features, "menu.tasks");

    return (
      <TaskListTable taskList={customerTaskList || []}/>
    );
  }
}
