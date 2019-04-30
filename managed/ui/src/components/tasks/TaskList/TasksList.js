// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { TaskListTable } from '../../tasks';
import { browserHistory} from 'react-router';
import { isNonAvailable } from 'utils/LayoutUtils';

export default class TasksList extends Component {

  componentWillMount() {
    this.props.fetchCustomerTasks();
    const { customer: { currentCustomer }} = this.props;
    if (isNonAvailable(currentCustomer.data.features, "tasks.display")) browserHistory.push('/');
  }

  render() {
    const {tasks: {customerTaskList}} = this.props;
    return (
      <TaskListTable taskList={customerTaskList}/>
    );
  }
}
