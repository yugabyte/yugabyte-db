// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';

export default class NodeDetails extends Component {
  static propTypes = {
    nodeDetails: PropTypes.object.isRequired
  };

  render() {
    const { nodeDetails } = this.props;
	  const nodeDetailRows = Object.keys(nodeDetails).map(function(key) {
      var nodeDetail = nodeDetails[key];
      return {
        name: nodeDetail.instance_name,
        region: nodeDetail.region,
        isMaster: nodeDetail.isMaster ? "Yes" : "No",
        isTServer: nodeDetail.isTserver ? "Yes" : "No",
        privateIP: nodeDetail.private_ip,
      };
    });

    return (
      <div className="row">
        <BootstrapTable data={nodeDetailRows}>
          <TableHeaderColumn dataField="name" isKey={true}>Instance Name</TableHeaderColumn>
          <TableHeaderColumn dataField="region">Region</TableHeaderColumn>
          <TableHeaderColumn dataField="isMaster">Master</TableHeaderColumn>
          <TableHeaderColumn dataField="isTServer">TServer</TableHeaderColumn>
          <TableHeaderColumn dataField="privateIP">Private IP</TableHeaderColumn>
        </BootstrapTable>
      </div>
    )
  }
}
