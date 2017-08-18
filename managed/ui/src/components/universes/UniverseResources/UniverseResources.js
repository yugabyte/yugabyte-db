// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isNonEmptyObject } from 'utils/ObjectUtils';
import { YBResourceCount, YBCost } from 'components/common/descriptors';
import PropTypes from 'prop-types';
import './UniverseResources.scss';

export default class UniverseResources extends Component {
  render() {
    const {resources, renderType} = this.props;
    let empty = true;
    let costPerDay = '$0.00';
    let costPerMonth = '$0.00';
    let numCores = 0;
    let memSizeGB = 0;
    let volumeSizeGB = 0;
    let volumeCount = 0;
    let universeNodes = <span/>;
    if (isNonEmptyObject(resources)) {
      empty = false;
      costPerDay = <YBCost value={resources.pricePerHour} multiplier={"day"} />;
      costPerMonth = <YBCost value={resources.pricePerHour} multiplier={"month"} />;
      numCores = resources.numCores;
      memSizeGB = resources.memSizeGB;
      volumeSizeGB = resources.volumeSizeGB;
      volumeCount = resources.volumeCount;
      if (resources && resources.numNodes && renderType === "Display") {
        universeNodes = <YBResourceCount size={resources.numNodes || 0} kind="Node" pluralizeKind />;
      }
    }
    
    return (
      <div className={"universe-resources "}>
        <span className={(empty ? 'empty' : '')}>
          {universeNodes}
          <YBResourceCount size={numCores || 0} kind="Core" pluralizeKind />
          <YBResourceCount size={memSizeGB || 0} unit="GB" kind="Memory" />
          <YBResourceCount size={volumeSizeGB || 0} unit="GB" kind="Storage" />
          <YBResourceCount size={volumeCount || 0} kind="Volume" pluralizeKind />
          <YBResourceCount size={costPerDay} kind="/day" />
          <YBResourceCount size={costPerMonth} kind="/month" />
        </span>
        {this.props.children}
      </div>
    );
  }
}

UniverseResources.PropTypes = {
  renderType: PropTypes.oneOf(["Display", "Configure"])
};

UniverseResources.defaultProps = {
  renderType: "Configure"
};
