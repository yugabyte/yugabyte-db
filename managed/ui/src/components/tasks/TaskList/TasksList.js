// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { TaskListTable } from '../../tasks';
import { showOrRedirect } from '../../../utils/LayoutUtils';

export default class TasksList extends Component {
  componentDidMount() {
    this.props.fetchCustomerTasks();
  }

  render() {
    const {
      tasks: { customerTaskList },
      customer: { currentCustomer }
    } = this.props;
    showOrRedirect(currentCustomer.data.features, 'menu.tasks');

    return (
      <TaskListTable
        taskList={customerTaskList || []}
        abortCurrentTask={this.props.abortCurrentTask}
        hideTaskAbortModal={this.props.hideTaskAbortModal}
        showTaskAbortModal={this.props.showTaskAbortModal}
        visibleModal={this.props.visibleModal}
      />
    );
  }
}
