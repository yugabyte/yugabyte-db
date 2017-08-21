// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { DescriptionList } from '../../common/descriptors';

export default class ResourceStringPanel extends Component {
  render() {
    const { universeInfo } = this.props;
    const { universeDetails } = universeInfo;
    const { userIntent } = universeDetails;
    const regionList = universeInfo.regions && universeInfo.regions.map(function(region) { return region.name; }).join(", ");
    const connectStringPanelItems = [
      {name: "Provider", data: universeInfo.provider && universeInfo.provider.name},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent && userIntent.instanceType},
      {name: "Replication Factor", data: userIntent.replicationFactor},
      {name: "SSH Key", data: userIntent.accessKeyCode}
    ];
    return (
      <DescriptionList listItems={connectStringPanelItems} />
    );
  }
}
