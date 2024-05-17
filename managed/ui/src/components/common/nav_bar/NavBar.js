// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import TopNavBar from './TopNavBar';
import SideNavBar from './SideNavBar';
import { RuntimeConfigKey } from '../../../redesign/helpers/constants';

import './stylesheets/NavBar.scss';

export default class NavBar extends Component {
  componentDidMount() {
    this.props.fetchCustomerRunTimeConfigs();
  }

  render() {
    const {
      customer: { customerRuntimeConfigs }
    } = this.props;
    const isTroubleshootingEnabled = customerRuntimeConfigs?.data?.configEntries?.some(
      (config) => config.key === RuntimeConfigKey.ENABLE_TROUBLESHOOTING && config.value === 'true'
    );

    return (
      <div className="yb-nav-bar">
        <TopNavBar customer={this.props.customer} logoutProfile={this.props.logoutProfile} />
        <SideNavBar
          customer={this.props.customer}
          enableBackupv2={this.props.enableBackupv2}
          isTroubleshootingEnabled={isTroubleshootingEnabled}
        />
      </div>
    );
  }
}
