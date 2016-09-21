// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Col } from 'react-bootstrap';
import DescriptionItem from '../DescriptionItem';

export default class HighlightedStatsPanel extends Component {
  render() {
    const { universe: { universeList, currentTotalCost, loading } } = this.props;
    
    if (loading) {
      return <div className="container">Loading...</div>;
    }
    
    return (
      <div className="row tile_count universe-cost-panel-container">
        <Col md={3} mdOffset={2} className="tile_stats_count">
          <DescriptionItem title={<span className="count_top">
                                   <i className="fa fa-globe"></i>
                                   Total Universes</span>}>
            <div className="count">{universeList.length}</div>
          </DescriptionItem>
        </Col>
        <Col md={3} className="tile_stats_count">
         <DescriptionItem title={<span className="count_top">
                                  <i className="fa fa-user"></i>
                                  Cost Per Month</span>}>
           <div className="count">${currentTotalCost.toFixed(2)}</div>
           <span className="count_bottom"><i className="red">
             <i className="fa fa-sort-desc"></i>0% </i>
             From last Week</span>
         </DescriptionItem>
        </Col>
        <Col md={3} className="tile_stats_count">
          <DescriptionItem title={<span className="count_top"><i className="fa fa-user"></i>
                                  Cost This Month</span>}>
            <div className="count">$__,__</div>
          </DescriptionItem>
        </Col>
      </div>
    )
  }
}
