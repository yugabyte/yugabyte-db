// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import NavBar from '../components/NavBar';

export default class DashboardWrapper extends Component {
  render() {
    return (
      <div>
        <NavBar />
        {this.props.children}
      </div>
    );
  }
};
