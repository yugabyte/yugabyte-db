// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import YBPanelItem from '../YBPanelItem';
import { ProgressBar } from 'react-bootstrap';

export default class UniverseCostBreakDownPanel extends Component {

  constructor(props) {
    super(props);
    props.fetchUniverseCost();
  }

  componentWillUnmount() {
    this.props.resetCustomerCost();
  }

  render() {
    const { universe: { universeCurrentCostList, currentTotalCost, loading } } = this.props;

    if (loading) {
      return <div className="container">Loading...</div>;
    }

    const getUniverseCostPercent = function(costPerMonth) {
      return currentTotalCost > 0 ? (costPerMonth/currentTotalCost*100).toFixed(2) : 0;
    }

    return (
       <YBPanelItem name="Monthly Cost Per Universe">
         <div className="x_content">
           {
             universeCurrentCostList.map(function(universeCostItem,idx){
               return (
                 <div key={universeCostItem.id+universeCostItem.name+idx} className="widget_summary">
                   <div className="w_left w_25">
                     <span>{universeCostItem.name}</span>
                   </div>
                   <div className="w_center w_55">
                     <div className="progress">
                       <ProgressBar now={universeCostItem.costPerMonth}
                                    label={getUniverseCostPercent(universeCostItem.costPerMonth)+"%"}
                                    max={currentTotalCost}
                                    bsStyle={"success"}/>
                     </div>
                   </div>
                   <div className="w_right w_20">
                     <span>${universeCostItem.costPerMonth.toFixed(2)}</span>
                   </div>
                   <div className="clearfix"></div>
                 </div>
               )
             })
           }
         </div>
       </YBPanelItem>
     )
   }
}
