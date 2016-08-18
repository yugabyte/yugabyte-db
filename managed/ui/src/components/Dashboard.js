// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import DashboardRightPane from './DashboardRightPane';
import TopNavBar from './TopNavBar';
import SideNavBar from './SideNavBar';

export default class Dashboard extends Component {
  static contextTypes = {
    router: PropTypes.object
  };

  componentWillMount() {
    if(typeof this.props.customer === "undefined" || this.props.customer.status !== "authenticated"){
			this.context.router.push('/login');
	  }
	}

  render() {
    return (
      <div className="dashboard">
        <TopNavBar />
        <SideNavBar />
        <div className="page-wrapper">
          <div className="row header-row">
            <DashboardRightPane {...this.props}/>
          </div>
        </div>
      </div>);
  }
}
