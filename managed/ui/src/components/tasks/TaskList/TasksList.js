// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { TaskListTable } from '../../tasks';
import { showOrRedirect } from '../../../utils/LayoutUtils';
import { YUGABYTE_TITLE } from '../../../config';

export default class TasksList extends Component {
  componentDidMount() {
    this.props.fetchCustomerTasks();
  }

  render() {
    const {
      tasks: { customerTaskList },
      customer: { currentCustomer, INSECURE_apiToken }
    } = this.props;
    showOrRedirect(currentCustomer.data.features, 'menu.tasks');
    const errorPlatformMessage = (
      <div className="oss-unavailable-warning">Only available on {YUGABYTE_TITLE}.</div>
    );

    return (
      <TaskListTable
        taskList={customerTaskList || []}
        overrideContent={INSECURE_apiToken && errorPlatformMessage}
        abortCurrentTask={this.props.abortCurrentTask}
        hideTaskAbortModal={this.props.hideTaskAbortModal}
        showTaskAbortModal={this.props.showTaskAbortModal}
        visibleModal={this.props.visibleModal}
      />
    );
  }
}
