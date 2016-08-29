// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import UniverseModalContainer from '../containers/UniverseModalContainer';
import ListUniverseContainer from '../containers/ListUniverseContainer';

export default class DashboardRightPane extends Component {
  render() {
    return (
      <div>
        <ListUniverseContainer />
        <UniverseModalContainer type="Create" />
      </div>
    );
  }
}
