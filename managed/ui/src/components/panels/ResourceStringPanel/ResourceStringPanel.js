// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { DescriptionList } from '../../common/descriptors';
import { getPrimaryCluster } from '../../../utils/UniverseUtils';
import { isNonEmptyArray, isNonEmptyObject } from '../../../utils/ObjectUtils';

export default class ResourceStringPanel extends Component {
  render() {
    const { universeInfo: {universeDetails: {clusters}}, providers } = this.props;
    const primaryCluster = getPrimaryCluster(clusters);
    const userIntent = primaryCluster && primaryCluster.userIntent;
    let provider = null;
    if (isNonEmptyObject(userIntent) && isNonEmptyArray(providers.data)) {
      provider = providers.data.find(item => item.code === userIntent.providerType);
    }
    const regionList = primaryCluster.regions && primaryCluster.regions.map((region) => region.name).join(", ");
    const connectStringPanelItems = [
      {name: "Provider", data: provider && provider.name},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent && userIntent.instanceType},
      {name: "Replication Factor", data: userIntent.replicationFactor},
      {name: "SSH Key", data: userIntent.accessKeyCode}
    ];
    return <DescriptionList listItems={connectStringPanelItems} />;
  }
}
