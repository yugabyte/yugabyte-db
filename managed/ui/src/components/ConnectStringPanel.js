// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Panel } from 'react-bootstrap';
import DescriptionList from './DescriptionList';
import { ROOT_URL } from '../config';

export default class UniverseInfoPanel extends Component {
  render() {
    const endpointUrl = ROOT_URL + "/customers/" + this.props.customerId +
                        "/universes/" + this.props.universeId + "/masters";
    const connect_string = "yb_load_test_tool --load_test_master_endpoint " + endpointUrl;
    var connectStringPanelItems = [
      {name: "Universe ID", data: this.props.universeId},
      {name: "Customer ID", data: this.props.customerId},
      {name: "Endpoint", data: endpointUrl},
      {name: "Load Test", data: connect_string, dataClass: "ybCodeSnippet well well-sm"}
    ];
    return (
      <Panel header="Basic Details" bsStyle="info">
        <div>
          <DescriptionList listItems={connectStringPanelItems} />
        </div>
      </Panel>);
  }
}

