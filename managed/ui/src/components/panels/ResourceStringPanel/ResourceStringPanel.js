// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { DescriptionList } from '../../common/descriptors';
import { getPrimaryCluster } from "../../../utils/UniverseUtils";

export default class ResourceStringPanel extends Component {
  render() {
    const { universeInfo, universeInfo: {universeDetails: {clusters}} } = this.props;
    const primaryCluster = getPrimaryCluster(clusters);
    const userIntent = primaryCluster && primaryCluster.userIntent;
    const regionList = primaryCluster.regions && primaryCluster.regions.map((region) => region.name).join(", ");
    const connectStringPanelItems = [
      {name: "Provider", data: userIntent.providerType && userIntent.providerType},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent && userIntent.instanceType},
      {name: "Replication Factor", data: userIntent.replicationFactor},
      {name: "SSH Key", data: userIntent.accessKeyCode}
    ];
    return <DescriptionList listItems={connectStringPanelItems} />;
  }
}
