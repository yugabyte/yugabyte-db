// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import DashboardRightPane from './DashboardRightPane';
import NavBar from './NavBar';

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
        <NavBar />
        <div className="page-wrapper">
          <div className="row header-row">
            <DashboardRightPane {...this.props}/>
          </div>
        </div>
      </div>);
  }
}
