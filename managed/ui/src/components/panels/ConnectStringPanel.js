// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { FormattedDate } from 'react-intl';
import { DescriptionList } from '../common/descriptors';
import { ROOT_URL } from '../../config';
import './stylesheets/connectStringPanel.css'

export default class connectStringPanel extends Component {
  render() {
    const {universeInfo, customerId} = this.props;
    var universeId = universeInfo.universeUUID;
    const endpointUrl = ROOT_URL + "/customers/" + customerId +
                        "/universes/" + universeId + "/masters";
    const connect_string = "yb_load_test_tool --load_test_master_endpoint " + endpointUrl;
    const endpoint =
      <a href={endpointUrl} target="_blank">Endpoint &nbsp;
        <i className="fa fa-external-link" aria-hidden="true"></i>
      </a>;
    var formattedCreationDate = <FormattedDate value={universeInfo.creationDate}
                                               year='numeric' month='long' day='2-digit'
                                               hour='2-digit' minute='2-digit' second='2-digit' timeZoneName='short' />
    var connectStringPanelItems = [
      {name: "Launch Time", data: formattedCreationDate},
      {name: "Universe ID", data: universeId},
      {name: "Customer ID", data: customerId},
      {name: "Meta Masters", data: endpoint},
      {name: "Load Test", data: connect_string, dataClass: "yb-code-snippet well well-sm"}
    ];
    return (
      <DescriptionList listItems={connectStringPanelItems} />
    );
  }
}
