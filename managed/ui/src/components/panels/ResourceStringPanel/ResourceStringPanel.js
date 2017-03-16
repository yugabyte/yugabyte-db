// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { DescriptionList } from '../../common/descriptors';
import { YBResourceCount, YBCost } from 'components/common/descriptors';

export default class ResourceStringPanel extends Component {
  render() {
    const { universeInfo } = this.props;
    const { universeDetails, resources } = universeInfo;
    const { userIntent } = universeDetails;
    var pricePerMonth = <YBCost value={resources.pricePerHour ? resources.pricePerHour : 0} multiplier={"month"}/> ;
    var connectStringPanelItems = [
      {name: "Nodes", data: userIntent.numNodes, nameClass: ""},
      {name: "Replication Factor", data: userIntent.replicationFactor},
      {name: "Memory", data: (resources.memSizeGB || 0) + " GB"},
      {name: "Cores", data: (resources.numCores || 0)},
      {name: "Storage", data: (resources.volumeSizeGB || 0) + " GB"},
      {name: "Volumes", data: (resources.volumeCount || 0)},
      {name: "Price Per Month", data: pricePerMonth}
    ];
    return (
      <DescriptionList listItems={connectStringPanelItems} />
    );
  }
}
