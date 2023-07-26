// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import DashboardContainer from '../components/dashboard/DashboardContainer';

class Dashboard extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <DashboardContainer />
      </div>
    );
  }
}
export default Dashboard;
