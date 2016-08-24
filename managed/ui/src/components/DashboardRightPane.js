// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import CreateUniverseContainer from '../containers/CreateUniverseContainer';
import ListUniverse from './ListUniverse';

export default class DashboardRightPane extends Component {
  render() {
    return (
      <div>
        <ListUniverse {...this.props} />
        <CreateUniverseContainer />
      </div>
    );
  }
}
