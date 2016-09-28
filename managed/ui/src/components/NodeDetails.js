// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import YBPanelItem from './YBPanelItem';

export default class NodeDetails extends Component {
  static propTypes = {
    nodeDetails: PropTypes.object.isRequired
  };

  render() {
    const { nodeDetails } = this.props;
    const nodeDetailRows = Object.keys(nodeDetails).map(function(key) {
      var nodeDetail = nodeDetails[key];
      var privateIP = nodeDetail.cloudInfo.private_ip;

      return {
        name: nodeDetail.nodeName,
        regionAz: `${nodeDetail.cloudInfo.region}/${nodeDetail.cloudInfo.az}`,
        isMaster: nodeDetail.isMaster ? "Yes" : "No",
        masterPort: nodeDetail.masterHttpPort,
        tserverPort: nodeDetail.tserverHttpPort,
        isTServer: nodeDetail.isTserver ? "Yes" : "No",
        privateIP: privateIP,
        nodeStatus: nodeDetail.state,
      };
    });

    var formatIpPort = function(cell, row, type) {
      if (cell === "No") {
        return <span>{cell}</span>;
      }
      var href;
      if (type === "master") {
        href = "http://" + row.privateIP + ":" + row.masterPort
      } else {
        href = "http://" + row.privateIP + ":" + row.tserverPort
      }
      return (<a href={href} target="_blank">{cell}</a>);
    }

    return (
      <YBPanelItem name="Node Details">
        <BootstrapTable data={nodeDetailRows}>
          <TableHeaderColumn dataField="name" isKey={true}>Instance Name</TableHeaderColumn>
          <TableHeaderColumn dataField="regionAz">Region/Zone</TableHeaderColumn>
          <TableHeaderColumn dataField="region">Region</TableHeaderColumn>
          <TableHeaderColumn
              dataField="isMaster"
              dataFormat={ formatIpPort }
              formatExtraData="master" >
            Master
          </TableHeaderColumn>
          <TableHeaderColumn
              dataField="isTServer"
              dataFormat={ formatIpPort }
              formatExtraData="tserver" >
            TServer
          </TableHeaderColumn>
          <TableHeaderColumn dataField="privateIP">Private IP</TableHeaderColumn>
          <TableHeaderColumn dataField="nodeStatus">Node Status</TableHeaderColumn>
        </BootstrapTable>
      </YBPanelItem>
    )
  }
}
