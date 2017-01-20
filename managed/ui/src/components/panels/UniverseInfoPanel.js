// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

import { DescriptionList } from '../common/descriptors';

export default class UniverseInfoPanel extends Component {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  render() {
    const { universeInfo } = this.props;
    const { universeDetails } = universeInfo;
    const { userIntent } = universeDetails;
    var azString = universeInfo.universeDetails.placementInfo.cloudList.map(function(cloudItem, idx){
      return cloudItem.regionList.map(function(regionItem, regionIdx){
        return regionItem.azList.map(function(azItem, azIdx){
          return azItem.name;
        }).join(", ")
      }).join(", ")
    }).join(", ");

    var regionList = universeInfo.regions.map(function(region) { return region.name; }).join(", ")
    var universeInfoItems = [
      {name: "Provider", data: universeInfo.provider.name},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent.instanceType},
      {name: "Availability Zones", data: azString},
      {name: "Replication Factor", data: userIntent.replicationFactor},
      {name: "Number Of Nodes", data: userIntent.numNodes}
    ];

    return (
      <DescriptionList listItems={universeInfoItems} />
    );
  }
}
