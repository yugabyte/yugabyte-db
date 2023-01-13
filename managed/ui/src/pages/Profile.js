// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { YBBanner, YBBannerVariant } from '../components/common/descriptors';
import { CustomerProfileContainer } from '../components/profile';

import './Profile.scss';

const BannerContent = () => (
  <>
    <b>Note!</b> “Users” page has moved. You can now{' '}
    <Link className="p-page-banner-link" to="/admin/user-management">
      access Users page
    </Link>{' '}
    from the User Management section under Admin menu.
  </>
);

class Profile extends Component {
  render() {
    return (
      <>
        <YBBanner
          className="p-page-banner"
          variant={YBBannerVariant.WARNING}
          showBannerIcon={false}
        >
          <BannerContent />
        </YBBanner>
        <div className="dashboard-container">
          <CustomerProfileContainer {...this.props} />
        </div>
      </>
    );
  }
}

export default Profile;
