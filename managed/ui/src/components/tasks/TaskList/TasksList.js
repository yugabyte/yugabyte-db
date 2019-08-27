// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { TaskListTable } from '../../tasks';
import { showOrRedirect } from 'utils/LayoutUtils';

export default class TasksList extends Component {

  componentWillMount() {
    this.props.fetchCustomerTasks();
  }

  render() {
    const {
      tasks: { customerTaskList },
      customer: { currentCustomer, INSECURE_apiToken }
    } = this.props;
    showOrRedirect(currentCustomer.data.features, "menu.tasks");
    const errorPlatformMessage = (
      <div className="oss-unavailable-warning">
        Only available on YugaByte Platform.
      </div>
    );

    return (
      <TaskListTable taskList={customerTaskList || []}
        overrideContent={INSECURE_apiToken && errorPlatformMessage}
      />
    );
  }
}
