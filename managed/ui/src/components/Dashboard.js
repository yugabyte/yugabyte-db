// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import DashboardRightPane from './DashboardRightPane';
import NavBar from './NavBar';

export default class Dashboard extends Component {
  static contextTypes = {
    router: PropTypes.object
  };

  render() {
    return (
      <div>
        <NavBar />
        <div id="page-wrapper">
          <div className="row header-row">
            <div className="col-lg-10">
              <h1>Dashboard</h1>
            </div>
          </div>
          <div>
            <DashboardRightPane {...this.props}/>
          </div>
        </div>
      </div>);
  }
}
