// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { FormattedDate } from 'react-intl';

import { DescriptionList } from '../../common/descriptors';

export default class UniverseInfoPanel extends Component {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  render() {
    const { universeInfo, customerId } = this.props;
    const { universeDetails } = universeInfo;
    const { userIntent } = universeDetails;
    var azString = universeInfo.universeDetails.placementInfo.cloudList.map(function(cloudItem, idx){
      return cloudItem.regionList.map(function(regionItem, regionIdx){
        return regionItem.azList.map(function(azItem, azIdx){
          return azItem.name;
        }).join(", ")
      }).join(", ")
    }).join(", ");

    var regionList = universeInfo.regions.map(function(region) { return region.name; }).join(", ");
    var universeId = universeInfo.universeUUID;
    var formattedCreationDate =
      <FormattedDate value={universeInfo.creationDate}
                     year='numeric' month='long' day='2-digit'
                     hour='2-digit' minute='2-digit' second='2-digit' timeZoneName='short' />;
    var universeInfoItems = [
      {name: "Provider", data: universeInfo.provider.name},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent.instanceType},
      {name: "Availability Zones", data: azString},
      {name: "Launch Time", data: formattedCreationDate},
      {name: "Universe ID", data: universeId},
      {name: "Customer ID", data: customerId},
    ];

    return (
      <DescriptionList listItems={universeInfoItems} />
    );
  }
}
