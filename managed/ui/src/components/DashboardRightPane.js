// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import CreateUniverse from './CreateUniverse';
import ListUniverse from './ListUniverse';

export default class DashboardRightPane extends Component {
  render() {
    return (
      <div>
        <ListUniverse {...this.props} />
        <CreateUniverse/>
      </div>
    );
  }
}
