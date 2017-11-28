// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import moment from 'moment';
import { YBFormattedNumber } from '../../common/descriptors';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoadingIcon } from '../../common/indicators';
import { YBStatsBlock } from '../../common/descriptors';
import './HighlightedStatsPanel.scss';
import {isDefinedNotNull, isNonEmptyObject} from "../../../utils/ObjectUtils";

export default class HighlightedStatsPanel extends Component {
  render() {
    const { universe: { universeList } } = this.props;
    let numNodes = 0;
    let totalCost = 0;
    if (getPromiseState(universeList).isLoading()) {
      return <YBLoadingIcon/>;
    }
    if (!(getPromiseState(universeList).isSuccess() || getPromiseState(universeList).isEmpty())) {
      return <span/>;
    }

    universeList.data.forEach(function (universeItem) {
      if (isNonEmptyObject(universeItem.universeDetails) && isNonEmptyObject(universeItem.universeDetails.userIntent)
          && isDefinedNotNull(universeItem.universeDetails.userIntent.numNodes)) {
        numNodes += universeItem.universeDetails.userIntent.numNodes;
      }
      if (isDefinedNotNull(universeItem.pricePerHour)) {
        totalCost += universeItem.pricePerHour * 24 * moment().daysInMonth();
      }
    });
    const formattedCost = (
      <YBFormattedNumber value={totalCost} maximumFractionDigits={2}
        formattedNumberStyle="currency" currency="USD"/>
    );
    return (
      <div className="tile_count highlighted-stats-panel">
        <YBStatsBlock value={universeList.data.length} label={"Universes"}/>
        <YBStatsBlock value={numNodes} label={"Nodes"}/>
        <YBStatsBlock value={formattedCost} label={"Per Month"}/>
      </div>
    );
  }
}
