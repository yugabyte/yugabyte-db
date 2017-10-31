// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Col } from 'react-bootstrap';
import moment from 'moment';
import { DescriptionItem, YBFormattedNumber } from '../../common/descriptors';
import { getPromiseState } from 'utils/PromiseUtils';
import {YBLoadingIcon} from '../../common/indicators';
import './HighlightedStatsPanel.scss';
import {isDefinedNotNull, isNonEmptyObject} from "../../../utils/ObjectUtils";

class StatsPanelComponent extends Component {
  render() {
    const {value, label} = this.props;
    return (
      <Col xs={4} className="tile_stats_count text-center">
        <DescriptionItem>
          <div className="count">
            {value}
          </div>
        </DescriptionItem>
        <span className="count_top">
          {label}
        </span>
      </Col>
    );
  }
}

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
      <div className="row tile_count highlighted-stats-panel">
        <Col xs={12} sm={12} mdOffset={2} md={8} lgOffset={4} lg={6}>
          <StatsPanelComponent value={universeList.data.length} label={"Universes"}/>
          <StatsPanelComponent value={numNodes} label={"Nodes"}/>
          <StatsPanelComponent value={formattedCost} label={"Per Month"}/>
        </Col>
      </div>
    );
  }
}
