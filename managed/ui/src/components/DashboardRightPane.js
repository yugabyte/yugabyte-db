// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import CreateUniverseContainer from '../containers/CreateUniverseContainer';
import ListUniverseContainer from '../containers/ListUniverseContainer';

export default class DashboardRightPane extends Component {
  render() {
    return (
      <div>
        <ListUniverseContainer />
        <CreateUniverseContainer />
      </div>
    );
  }
}
