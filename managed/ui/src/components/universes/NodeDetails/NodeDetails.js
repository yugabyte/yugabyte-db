// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBPanelItem } from '../../panels';
import { isValidObject, isValidArray, isDefinedNotNull } from 'utils/ObjectUtils';
import NodeConnectModal from './NodeConnectModal';

export default class NodeDetails extends Component {
  static propTypes = {
    nodeDetails: PropTypes.array.isRequired
  };

  componentWillReceiveProps(nextProps) {
    if (isValidObject(this.refs.nodeDetailTable)) {
      this.refs.nodeDetailTable.handleSort('asc', 'name');
    }
  }

  render() {
    const { nodeDetails } = this.props;

    if (!isValidArray(nodeDetails)) {
      return <span />;
    }
    const nodeDetailRows = nodeDetails.filter(function(nodeDetail){
        return nodeDetail.state !== "Destroyed"
      }).map(function(nodeDetail){
       return {
          name: nodeDetail.nodeName,
          regionAz: `${nodeDetail.cloudInfo.region}/${nodeDetail.cloudInfo.az}`,
          isMaster: nodeDetail.isMaster ? "Yes" : "No",
          masterPort: nodeDetail.masterHttpPort,
          tserverPort: nodeDetail.tserverHttpPort,
          isTServer: nodeDetail.isTserver ? "Yes" : "No",
          privateIP: nodeDetail.cloudInfo.private_ip,
          publicIP: nodeDetail.cloudInfo.public_ip,
          nodeStatus: nodeDetail.state,
          cloudInfo: nodeDetail.cloudInfo
        }
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

    const nodeIPs = nodeDetailRows.map(function(node) {
      if (isDefinedNotNull(node.privateIP) && isDefinedNotNull(node.publicIP)) {
        return { privateIP: node.privateIP, publicIP: node.publicIP }
      } else {
        return null
      }
    }).filter(Boolean);

    var getNodeNameLink = function(cell, row) {
      if (row.cloudInfo.cloud === "aws") {
        var awsURI = `https://${row.cloudInfo.region}.console.aws.amazon.com/ec2/v2/home?region=${row.cloudInfo.region}#Instances:search=${cell};sort=availabilityZone`;
        return <a href={awsURI} target="_blank">{cell}</a>
      } else {
        return cell;
      }
    }
    return (
      <YBPanelItem name="Node Details">
        { nodeIPs && <NodeConnectModal nodeIPs={nodeIPs} />}
        <BootstrapTable ref='nodeDetailTable' data={nodeDetailRows} >
          <TableHeaderColumn dataField="name" isKey={true} dataFormat={getNodeNameLink}>Instance Name</TableHeaderColumn>
          <TableHeaderColumn dataField="regionAz">Region/Zone</TableHeaderColumn>
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
