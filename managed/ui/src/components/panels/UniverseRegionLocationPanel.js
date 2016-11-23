// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {isValidArray} from '../../utils/ObjectUtils';
import RegionMap from '../maps/RegionMap';

export default class UniverseRegionLocationPanel extends Component {

  render() {
    const { cloud } = this.props;
    if (!isValidArray(cloud.supportedRegionList)) {
      return <span/>
    }
    return (
      <RegionMap title="All Supported Regions" regions={cloud.supportedRegionList} type="all"/>
    )
  }
}
