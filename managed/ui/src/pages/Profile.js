// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { CustomerProfileContainer } from '../components/profile';

import './Profile.scss';

class Profile extends Component {
  render() {
    return <CustomerProfileContainer {...this.props} />;
  }
}

export default Profile;
