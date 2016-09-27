// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { FormattedDate } from 'react-intl';
import DescriptionList from './DescriptionList';
import YBPanelItem from './YBPanelItem';

export default class UniverseInfoPanel extends Component {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  render() {
    const { universeInfo } = this.props;
    const { universeDetails } = universeInfo;
    const { userIntent } = universeDetails;

    var formattedCreationDate = <FormattedDate value={universeInfo.creationDate}
      year='numeric' month='long' day='2-digit'
      hour='2-digit' minute='2-digit' second='2-digit' timeZoneName='short' />

    var regionList = universeInfo.regions.map(function(region) { return region.name; }).join(", ")
    var universeInfoItems = [
      {name: "Launch Time", data: formattedCreationDate},
      {name: "Provider", data: universeInfo.provider.name},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent.instanceType},
      {name: "Multi AZ Enabled", data: userIntent.isMultiAZ ? "Yes" : "No"},
      {name: "Replication Factor", data: userIntent.replicationFactor}
    ];

    return (
      <YBPanelItem name="Universe Configuration">
        <DescriptionList listItems={universeInfoItems} />
      </YBPanelItem>
    );
  }
}
