// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {withRouter} from 'react-router';
import {isNonEmptyArray} from 'utils/ObjectUtils';
const PropTypes = require('prop-types');

class AuthenticatedComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {prevPath: ""};
  }

  getChildContext() {
    return {prevPath: this.state.prevPath};
  }

  componentWillMount() {
    this.props.fetchHostInfo();
    this.props.fetchSoftwareVersions();
    this.props.fetchTableColumnTypes();
    this.props.fetchUniverseList();
    this.props.getEBSListItems();
    this.props.getProviderListItems();
    this.props.getSupportedRegionList();
    this.props.getYugaWareVersion();
    this.props.fetchCustomerTasks();
    this.props.fetchCustomerConfigs();
  }

  componentWillUnmount() {
    this.props.resetUniverseList();
  }

  hasPendingCustomerTasks = taskList => {
    return isNonEmptyArray(taskList) ? taskList.some((task) => ((task.status === "Running" ||
    task.status === "Initializing") && (Number(task.percentComplete) !== 100))) : false;
  };

  componentWillReceiveProps(nextProps) {
    const {tasks} = nextProps;
    if (this.props.fetchMetadata !== nextProps.fetchMetadata && nextProps.fetchMetadata) {
      this.props.getProviderListItems();
      this.props.fetchUniverseList();
      this.props.getSupportedRegionList();
    }
    if (this.props.fetchUniverseMetadata !== nextProps.fetchUniverseMetadata && nextProps.fetchUniverseMetadata) {
      this.props.fetchUniverseList();
    }
    if (this.props.location !== nextProps.location) {
      this.setState({prevPath: this.props.location.pathname});
    }
    // If there is a pending customer task, we start schedule fetch
    if (this.hasPendingCustomerTasks(tasks.customerTaskList)) {
      if (typeof (this.timeout) === "undefined") {
        this.scheduleFetch();
      }
    } else {
      // If there is no pending task, we clear the timer
      if (typeof (this.timeout) !== "undefined") {
        clearTimeout(this.timeout);
      }
    }
  }

  scheduleFetch = () => {
    const self = this;
    this.timeout = setInterval(function(){
      self.props.fetchCustomerTasks();
    }, 6000);
  };

  render() {
    return (
      <div className="full-height-container">
        {this.props.children}
      </div>
    );
  }
}

export default withRouter(AuthenticatedComponent);

AuthenticatedComponent.childContextTypes = {
  prevPath: PropTypes.string
};
