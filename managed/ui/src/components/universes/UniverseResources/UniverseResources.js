// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isDefinedNotNull } from 'utils/ObjectUtils';
import { YBResourceCount, YBCost } from 'components/common/descriptors';

import './UniverseResources.scss';

export default class UniverseResources extends Component {
  render() {
    const {resources} = this.props;
    if (!isDefinedNotNull(resources)) {
      return <span/>;
    }

    var empty = false;
    var costPerDay = '$0.00';
    var costPerMonth = '$0.00';
    if (isDefinedNotNull(resources) && Object.keys(resources).length > 0) {
      costPerDay = <YBCost value={resources.pricePerHour} multiplier={"day"} />
      costPerMonth = <YBCost value={resources.pricePerHour} multiplier={"month"} />
    } else {
      empty = !(resources.numCores || resources.memSizeGB ||
        resources.volumeSizeGB || resources.volumeCount);
    }
    return (
      <div className={"universe-resources " + (empty ? 'empty' : '')}>
        <YBResourceCount size={resources.numCores || 0} kind="Core" pluralizeKind />
        <YBResourceCount size={resources.memSizeGB || 0} unit="GB" kind="Memory" />
        <YBResourceCount size={resources.volumeSizeGB || 0} unit="GB" kind="Storage" />
        <YBResourceCount size={resources.volumeCount || 0} kind="Volume" pluralizeKind />
        <YBResourceCount size={costPerDay} kind="/day" />
        <YBResourceCount size={costPerMonth} kind="/month" />
        <hr/>
      </div>
    );
  }
}
