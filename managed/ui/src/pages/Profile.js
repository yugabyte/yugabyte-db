// Copyright (c) YugaByte, Inc.

import { Component, lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';
import './Profile.scss';

const CustomerProfileContainer = lazy(() =>
  import('../components/profile/CustomerProfileContainer')
);

class Profile extends Component {
  render() {
    return (
      <Suspense fallback={YBLoadingCircleIcon}>
        <CustomerProfileContainer {...this.props} />
      </Suspense>
    );
  }
}

export default Profile;
