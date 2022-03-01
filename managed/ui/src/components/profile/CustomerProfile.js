// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { browserHistory } from 'react-router';
import { isNonAvailable, showOrRedirect } from '../../utils/LayoutUtils';
import UserProfileForm from './UserProfileForm';
import { YBLoading } from '../common/indicators';
import { getPromiseState } from '../../utils/PromiseUtils';

export default class CustomerProfile extends Component {
  constructor(props) {
    super(props);
    this.state = {
      statusUpdated: false,
      updateStatus: ''
    };
  }

  componentDidMount() {
    const { customer } = this.props;
    this.props.getCustomerUsers();
    this.props.validateRegistration();
    if (isNonAvailable(customer.features, 'main.profile')) browserHistory.push('/');
  }

  handleProfileUpdate = (status) => {
    this.setState({
      statusUpdated: true,
      updateStatus: status
    });
  };

  render() {
    const { customer = {}, apiToken, customerProfile } = this.props;
    if (getPromiseState(customer).isLoading() || getPromiseState(customer).isInit()) {
      return <YBLoading />;
    }
    showOrRedirect(customer.data.features, 'main.profile');

    let profileUpdateStatus = <span />;
    if (this.state.statusUpdated) {
      if (this.state.updateStatus === 'updated-success') {
        profileUpdateStatus = (
          <span className="pull-right request-status yb-success-color yb-dissapear">
            Profile Updated Successfully
          </span>
        );
      } else {
        profileUpdateStatus = (
          <span className="pull-right request-status yb-fail-color yb-dissapear">
            Profile Update Failed
          </span>
        );
      }
      setTimeout(() => {
        this.setState({ statusUpdated: false });
      }, 2000);
    }

    return (
      <div className="tab-content">
        <h2 className="content-title">User Profile {profileUpdateStatus}</h2>
        <UserProfileForm
          customer={this.props.customer}
          customerProfile={customerProfile}
          apiToken={apiToken}
          handleProfileUpdate={this.handleProfileUpdate}
          {...this.props}
        />
      </div>
    );
  }
}
