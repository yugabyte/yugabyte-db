// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import ListUniverseContainer from '../containers/ListUniverseContainer';

export default class DashboardRightPane extends Component {
  render() {
    return (
      <div>
        <ListUniverseContainer />
      </div>
    );
  }
}
