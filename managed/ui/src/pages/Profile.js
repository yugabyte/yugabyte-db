// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { CustomerProfileContainer } from '../components/profile';

class Profile extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <CustomerProfileContainer {...this.props} />
      </div>
    );
  }
}

export default Profile;
