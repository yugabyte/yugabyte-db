// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { ReleaseListContainer } from '../components/releases';

class Releases extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <ReleaseListContainer />
      </div>
    );
  }
}

export default Releases;
