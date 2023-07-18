// Copyright (c) YugaByte, Inc.

import { Component } from 'react';

import { YugawareLogsContainer } from '../components/yugaware_logs';

class YugawareLogs extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <YugawareLogsContainer />
      </div>
    );
  }
}

export default YugawareLogs;
