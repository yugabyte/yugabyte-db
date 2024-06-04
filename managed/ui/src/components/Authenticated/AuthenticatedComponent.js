// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import _ from 'lodash';
import { withRouter } from 'react-router';
import { isNonEmptyArray } from '../../utils/ObjectUtils';
import { getPromiseState } from '../../utils/PromiseUtils';
import { isHidden } from '../../utils/LayoutUtils';
import PropTypes from 'prop-types';
import { YBModal, YBCheckBox } from '../common/forms/fields';

class AuthenticatedComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {
      prevPath: '',
      fetchScheduled: false,
      notificationsHidden: false,
      hideNotificationsChecked: false
    };
  }

  getChildContext() {
    return { prevPath: this.state.prevPath };
  }

  componentDidMount() {
    // Remove old releases API call - 2024.2
    this.props.fetchSoftwareVersions();
    this.props.fetchDBVersions();
    this.props.fetchTableColumnTypes();
    this.props.getEBSListItems();
    this.props.getGCPListItems();
    this.props.getAZUListItems();
    this.props.getProviderListItems();
    this.props.getSupportedRegionList();
    this.props.getYugaWareVersion();
    this.props.fetchCustomerTasks();
    this.props.fetchCustomerCertificates();
    this.props.fetchCustomerConfigs();
    this.props.fetchRuntimeConfigKeyInfo();
    this.props.fetchUser();
    this.props.fetchAdminNotifications();
  }

  componentWillUnmount() {
    this.props.resetUniverseList();
  }

  hasPendingCustomerTasks = (taskList) => {
    return isNonEmptyArray(taskList)
      ? taskList.some(
          (task) =>
            (task.status === 'Running' || task.status === 'Initializing') &&
            Number(task.percentComplete) !== 100
        )
      : false;
  };

  componentDidUpdate(prevProps) {
    const { tasks } = this.props;
    if (prevProps.fetchMetadata !== this.props.fetchMetadata && this.props.fetchMetadata) {
      this.props.getProviderListItems();
      this.props.fetchUniverseList();
      this.props.getSupportedRegionList();
    }
    if (
      prevProps.fetchUniverseMetadata !== this.props.fetchUniverseMetadata &&
      this.props.fetchUniverseMetadata
    ) {
      this.props.fetchUniverseList();
    }
    if (prevProps.location !== this.props.location) {
      this.setState({ prevPath: prevProps.location.pathname });
    }
    // Check if there are pending customer tasks and no existing recursive fetch calls
    if (this.hasPendingCustomerTasks(tasks.customerTaskList) && !this.state.fetchScheduled) {
      this.scheduleFetch();
    }
  }

  scheduleFetch = () => {
    const self = this;

    function queryTasks() {
      const taskList = self.props.tasks.customerTaskList;

      // Check if there are still customer tasks in progress or if list is empty
      if (!self.hasPendingCustomerTasks(taskList) && isNonEmptyArray(taskList)) {
        self.setState({ fetchScheduled: false });
      } else {
        self.props.fetchCustomerTasks().then(() => {
          setTimeout(queryTasks, 6000);
        });
      }
    }

    queryTasks();
    this.setState({ fetchScheduled: true });
  };

  closeNotificationsModal = () => {
    const { adminNotifications } = this.props;
    _.forEach(adminNotifications?.data?.messages || [], (message) => {
      if (this.state.hideNotificationsChecked) {
        localStorage.setItem(message.code, 'hidden');
      }
    });
    this.setState({ notificationsHidden: true });
  };

  render() {
    const { currentCustomer, adminNotifications, location } = this.props;
    const { notificationsHidden } = this.state;
    const sidebarHidden =
      getPromiseState(currentCustomer).isSuccess() &&
      isHidden(currentCustomer.data.features, 'menu.sidebar');
    const showNotifications =
      getPromiseState(adminNotifications).isSuccess() &&
      !notificationsHidden &&
      adminNotifications?.data?.messages?.length > 0 &&
      !location.query['hide-notifications'];
    let notificationText = '';
    if (showNotifications) {
      _.forEach(adminNotifications?.data?.messages || [], (message) => {
        if (
          localStorage.getItem(message.code) === null &&
          localStorage.getItem(message.code) !== 'hidden'
        ) {
          notificationText += `<span>${message.htmlMessage}</span>`;
        }
      });
    }
    const notificationVisible = showNotifications && notificationText.length > 0;

    const notificationMessageStatus = (
      <div className="footer-accessory-wrapper">
        <YBCheckBox
          label={'Do not show this message in the future'}
          onClick={() => this.setState({ hideNotificationsChecked: true })}
        />
      </div>
    );
    return (
      <div
        className={sidebarHidden ? 'full-height-container sidebar-hidden' : 'full-height-container'}
      >
        {this.props.children}
        <YBModal
          title="Notification"
          visible={notificationVisible}
          onHide={this.closeNotificationsModal}
          showCancelButton={true}
          cancelLabel={'Close'}
          footerAccessory={notificationMessageStatus}
        >
          <div
            onClick={this.notificationLinkHandler}
            dangerouslySetInnerHTML={{ __html: notificationText }}
          />
        </YBModal>
      </div>
    );
  }
}

export default withRouter(AuthenticatedComponent);

AuthenticatedComponent.childContextTypes = {
  prevPath: PropTypes.string
};
