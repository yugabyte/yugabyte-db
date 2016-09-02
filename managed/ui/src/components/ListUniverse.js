// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import UniverseTableContainer from '../containers/UniverseTableContainer';
import YBPanelItem from './YBPanelItem';
export default class ListUniverse extends Component {
  render(){
    return (
      <div>
        <YBPanelItem name="Universe List">
          <UniverseTableContainer/>
        </YBPanelItem>
      </div>
    )
  }
}
