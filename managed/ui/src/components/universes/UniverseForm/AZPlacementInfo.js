// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { isValidArray, isValidObject } from 'utils/ObjectUtils';
const statusTypes =
{
  singleRF: {currentStatusIcon: "fa fa-exclamation", currentStatusString: "Primary data placement is not redundant," +
             " universe cannot survive even 1 node failure", currentStatusClass: "yb-warn-color"},
  azWarning: {currentStatusIcon: "fa fa-exclamation", currentStatusString: "Primary data placement is not geo-redundant," +
             " universe cannot survive even 1 availability zone failure", currentStatusClass: "yb-warn-color"},
  regionWarning: {currentStatusIcon: "fa fa-check", currentStatusString: "Primary data placement is geo-redundant," +
                  " universe can survive at least 1 availability zone failure", currentStatusClass: "yb-success-color"},
  multiRegion: {currentStatusIcon: "fa fa-check", currentStatusString: "Primary data placement is fully geo-redundant," +
                " universe can survive at least 1 region failure", currentStatusClass: "yb-success-color"},
  
}
export default class AZSelectorTable extends Component {
  static propTypes = {
    placementInfo: PropTypes.object.isRequired
  }
  render() {
    const {placementInfo} = this.props;
    if (!isValidObject(placementInfo) || !isValidArray(Object.keys(placementInfo))) {
      return <span/>;
    }
    var currentStatusType = "";
    if (placementInfo.replicationFactor === 1) {
      currentStatusType = "singleRF";
    } else {
      if (placementInfo.numUniqueAzs <= 2) {
        currentStatusType = "azWarning";
      } else {
        if (placementInfo.numUniqueRegions < 2) {
          currentStatusType = "regionWarning";
        } else {
          currentStatusType = "multiRegion";
        }
      }
    }
    return (
      <div>
        <span className={statusTypes[currentStatusType].currentStatusClass}>&nbsp;<i className={statusTypes[currentStatusType].currentStatusIcon}/>&nbsp;{statusTypes[currentStatusType].currentStatusString}</span>
      </div>
    )
  }
}
