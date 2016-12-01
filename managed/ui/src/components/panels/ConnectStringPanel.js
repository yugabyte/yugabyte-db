// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { DescriptionList } from '../common/descriptors';
import { ROOT_URL } from '../../config';
import { YBPanelItem } from '.';

export default class UniverseInfoPanel extends Component {
  render() {
    const endpointUrl = ROOT_URL + "/customers/" + this.props.customerId +
                        "/universes/" + this.props.universeId + "/masters";
    const connect_string = "yb_load_test_tool --load_test_master_endpoint " + endpointUrl;
    const endpoint =
      <a href={endpointUrl} target="_blank">Endpoint &nbsp;
        <i className="fa fa-external-link" aria-hidden="true"></i>
      </a>;

    var connectStringPanelItems = [
      {name: "Universe ID", data: this.props.universeId},
      {name: "Customer ID", data: this.props.customerId},
      {name: "Meta Masters", data: endpoint},
      {name: "Load Test", data: connect_string, dataClass: "ybCodeSnippet well well-sm"}
    ];
    return (
      <YBPanelItem name="Basic Details">
        <DescriptionList listItems={connectStringPanelItems} />
      </YBPanelItem>
    );
  }
}
