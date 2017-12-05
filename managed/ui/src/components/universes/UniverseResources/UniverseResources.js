// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isNonEmptyObject } from 'utils/ObjectUtils';
import { YBResourceCount, YBCost } from 'components/common/descriptors';
import PropTypes from 'prop-types';
import './UniverseResources.scss';
import { FlexContainer, FlexShrink, FlexGrow } from '../../common/flexbox/YBFlexBox';

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
      memSizeGB = resources.memSizeGB ? resources.memSizeGB.toFixed(2) : 0;
      volumeSizeGB = resources.volumeSizeGB ? resources.volumeSizeGB.toFixed(2) : 0;
      volumeCount = resources.volumeCount;
      if (resources && resources.numNodes && renderType === "Display") {
        universeNodes = <YBResourceCount size={resources.numNodes || 0} kind="Node" pluralizeKind />;
      }
    }
    
    return (
      <FlexContainer>
        <FlexGrow>
          <div className={(empty ? "universe-resources empty" : "universe-resources ")}>
            {universeNodes}
            <YBResourceCount size={numCores || 0} kind="Core" pluralizeKind />
            <YBResourceCount size={memSizeGB || 0} unit="GB" kind="Memory" />
            <YBResourceCount size={volumeSizeGB || 0} unit="GB" kind="Storage" />
            <YBResourceCount size={volumeCount || 0} kind="Volume" pluralizeKind />
            <YBResourceCount size={costPerDay} kind="/day" />
            <YBResourceCount size={costPerMonth} kind="/month" />
          </div>
        </FlexGrow>
        <FlexShrink style={{ padding: 10, paddingBottom: 0 , paddingRight: 7 }}>
          {this.props.children}
        </FlexShrink>
      </FlexContainer>
    );
  }
}

UniverseResources.PropTypes = {
  renderType: PropTypes.oneOf(["Display", "Configure"])
};

UniverseResources.defaultProps = {
  renderType: "Configure"
};
