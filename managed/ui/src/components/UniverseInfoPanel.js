// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Panel } from 'react-bootstrap';
import * as moment from 'moment';
import DescriptionList from './DescriptionList';

export default class UniverseInfoPanel extends Component {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  render() {
    const { universeInfo } = this.props;
    const { universeDetails } = universeInfo;
    const { userIntent } = universeDetails;
    var regionList = universeInfo.regions.map(function(region) { return region.name; }).join(", ")
    var universeInfoItems = [
      {name: "Launch Time", data: moment.default(universeInfo.creationDate).format('LLLL')},
      {name: "Provider", data: universeInfo.provider.name},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent.instanceType},
      {name: "Multi AZ Enabled", data: userIntent.isMultiAZ ? "Yes" : "No"},
      {name: "Replication Factor", data: userIntent.replicationFactor}
    ];

    return (
      <Panel header="Universe Info" bsStyle="info">
        <DescriptionList listItems={universeInfoItems} />
      </Panel>);
  }
}
