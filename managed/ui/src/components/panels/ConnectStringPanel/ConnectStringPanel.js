// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { DescriptionList } from '../../common/descriptors';
import { ROOT_URL } from '../../../config';
import './connectStringPanel.css';

export default class ConnectStringPanel extends Component {
  render() {
    const { universeInfo, customerId, universeInfo: {universeDetails: {userIntent}}} = this.props;
    var universeId = universeInfo.universeUUID;
    const endpointUrl = ROOT_URL + "/customers/" + customerId +
                        "/universes/" + universeId + "/masters";

    const endpoint =
      <a href={endpointUrl} target="_blank">Endpoint &nbsp;
        <i className="fa fa-external-link" aria-hidden="true"></i>
      </a>;
    var connectStringPanelItems = [
      {name: "Meta Masters", data: endpoint},
      {name: "Server Version", data: userIntent.ybSofwareVersion},
    ];
    return (
      <DescriptionList listItems={connectStringPanelItems} />
    );
  }
}
