// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import TopNavBar from './TopNavBar';
import SideNavBar from './SideNavBar';
import './stylesheets/NavBar.scss';
import { RuntimeConfigKey } from '../../../redesign/helpers/constants';

export default class NavBar extends Component {
  componentDidMount() {
    this.props.fetchCustomerRunTimeConfigs();
  }

  render() {
    // const { customerRuntimeConfigs } = this.props;
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
