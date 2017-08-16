// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Col } from 'react-bootstrap';
import moment from 'moment';
import { DescriptionItem, YBFormattedNumber } from '../../common/descriptors';
import { getPromiseState } from 'utils/PromiseUtils';
import {YBLoadingIcon} from '../../common/indicators';
import './HighlightedStatsPanel.css'

class StatsPanelComponent extends Component {
  render() {
    const {value, label} = this.props;
    return (
      <Col sm={4} className="tile_stats_count text-center">
        <DescriptionItem>
          <div className="count">
            {value}
          </div>
        </DescriptionItem>
        <span className="count_top">
          {label}
        </span>
      </Col>
    )
  }
}

export default class HighlightedStatsPanel extends Component {
  render() {
    const { universe: { universeList } } = this.props;
    var numNodes = 0;
    var totalCost = 0;
    if (getPromiseState(universeList).isLoading()) {
      return <YBLoadingIcon/>;
    }
    if (!(getPromiseState(universeList).isSuccess() || getPromiseState(universeList).isEmpty())) {
      return <span/>
    }

    universeList.data.forEach(function (universeItem) {
      numNodes += universeItem.universeDetails.userIntent.numNodes;
      totalCost += universeItem.pricePerHour * 24 * moment().daysInMonth();
    });
    const formattedCost = (
      <YBFormattedNumber value={totalCost} maximumFractionDigits={2}
        formattedNumberStyle="currency" currency="USD"/>
    );
    return (
      <div className="row tile_count highlighted-stats-panel">
        <Col sm={6} smOffset={3}>
          <StatsPanelComponent value={universeList.data.length} label={"Universes"}/>
          <StatsPanelComponent value={numNodes} label={"Nodes"}/>
          <StatsPanelComponent value={formattedCost} label={"Per Month"}/>
        </Col>
      </div>
    )
  }
}
