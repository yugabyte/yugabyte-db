// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { DescriptionList } from '../../common/descriptors';

export default class ResourceStringPanel extends Component {
  render() {
    const { universeInfo } = this.props;
    const { universeDetails } = universeInfo;
    const { userIntent } = universeDetails;
    const azString = universeInfo.universeDetails.placementInfo.cloudList.map(function(cloudItem, idx){
      return cloudItem.regionList.map(function(regionItem, regionIdx){
        return regionItem.azList.map(function(azItem, azIdx){
          return azItem.name;
        }).join(", ");
      }).join(", ");
    }).join(", ");
    const regionList = universeInfo.regions && universeInfo.regions.map(function(region) { return region.name; }).join(", ");
    const connectStringPanelItems = [
      {name: "Provider", data: universeInfo.provider && universeInfo.provider.name},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent && userIntent.instanceType},
      {name: "Availability Zones", data: azString},
      {name: "Replication Factor", data: userIntent.replicationFactor},
    ];
    return (
      <DescriptionList listItems={connectStringPanelItems} />
    );
  }
}
