// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBPanelItem } from '../../panels';
import { isValidObject, isNonEmptyArray, isDefinedNotNull } from 'utils/ObjectUtils';
import NodeConnectModal from './NodeConnectModal';
import {isNodeRemovable} from 'utils/UniverseUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';

export default class NodeDetails extends Component {

  componentWillReceiveProps(nextProps) {
    if (isValidObject(this.refs.nodeDetailTable)) {
      this.refs.nodeDetailTable.handleSort('asc', 'name');
    }
  }

  componentWillMount() {
    let uuid = this.props.uuid;
    if (typeof this.props.universeSelectionId !== "undefined") {
      uuid = this.props.universeUUID;
    }
    this.props.getUniversePerNodeStatus(uuid);
  }

  deleteNode(node) {
    const {universe: {currentUniverse}} = this.props;
    const universeUUID = currentUniverse.data.universeUUID;
    this.props.deleteNode(node.name, universeUUID);
  }

  render() {
    const { universe: {currentUniverse, universePerNodeStatus}} = this.props;
    const self = this;
    const nodeDetails = currentUniverse.data.universeDetails.nodeDetailsSet;
    if (!isNonEmptyArray(nodeDetails)) {
      return <span />;
    }
    const nodeDetailRows = nodeDetails.map(function(nodeDetail){
      return {
        name: nodeDetail.nodeName,
        regionAz: `${nodeDetail.cloudInfo.region}/${nodeDetail.cloudInfo.az}`,
        isMaster: nodeDetail.isMaster ? "Details" : "-",
        masterPort: nodeDetail.masterHttpPort,
        tserverPort: nodeDetail.tserverHttpPort,
        isTServer: nodeDetail.isTserver ? "Details" : "-",
        privateIP: nodeDetail.cloudInfo.private_ip,
        publicIP: nodeDetail.cloudInfo.public_ip,
        nodeStatus: nodeDetail.state,
        cloudInfo: nodeDetail.cloudInfo
      };
    });

    const loadingIcon = <i className="fa fa-spinner fa-spin" />;
    const successIcon = <i className="fa fa-check-circle yb-success-color" />;
    const warningIcon = <i className="fa fa-warning yb-fail-color" />;

    const formatIpPort = function(cell, row, type) {
      if (cell === "-") {
        return <span>{cell}</span>;
      }
      const isMaster = type === "master";
      const href = "http://" + row.privateIP + ":" + (isMaster ? row.masterPort : row.tserverPort);
      const promiseState = getPromiseState(universePerNodeStatus);
      const inLoadingState = promiseState.isLoading() ||promiseState.isInit();
      let statusDisplay = inLoadingState ? loadingIcon : warningIcon;
      if (isDefinedNotNull(currentUniverse.data) && !inLoadingState) {
        if (isDefinedNotNull(universePerNodeStatus.data) &&
            universePerNodeStatus.data.universe_uuid === currentUniverse.data.universeUUID &&
            isDefinedNotNull(universePerNodeStatus.data[row.name]) &&
            universePerNodeStatus.data[row.name][isMaster ? "master_alive" : "tserver_alive"]) {
          statusDisplay = successIcon;
        }
        return <span>{statusDisplay}&nbsp;<a href={href} target="_blank" rel="noopener noreferrer">{cell}</a></span>;
      } else {
        return <span>{statusDisplay}&nbsp;{cell}</span>;
      }
    };

    const nodeIPs = nodeDetailRows.map(function(node) {
      if (isDefinedNotNull(node.privateIP) && isDefinedNotNull(node.publicIP)) {
        return { privateIP: node.privateIP, publicIP: node.publicIP };
      } else {
        return null;
      }
    }).filter(Boolean);

    const getNodeNameLink = function(cell, row) {
      if (row.cloudInfo.cloud === "aws") {
        const awsURI = `https://${row.cloudInfo.region}.console.aws.amazon.com/ec2/v2/home?region=${row.cloudInfo.region}#Instances:search=${cell};sort=availabilityZone`;
        return <a href={awsURI} target="_blank" rel="noopener noreferrer">{cell}</a>;
      } else {
        return cell;
      }
    };

    const getNodeAction = function(cell, row) {
      if (isNodeRemovable(cell)) {
        return <div className="node-action-text" onClick={self.deleteNode.bind(self, row)}>Delete</div>;
      }
    };

    return (
      <YBPanelItem name="Nodes">
        { nodeIPs && <NodeConnectModal nodeIPs={nodeIPs} />}
        <BootstrapTable ref='nodeDetailTable' data={nodeDetailRows} >
          <TableHeaderColumn dataField="name" isKey={true} dataFormat={getNodeNameLink}>Name</TableHeaderColumn>
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
          <TableHeaderColumn dataField="nodeStatus">Status</TableHeaderColumn>
          <TableHeaderColumn dataField="nodeStatus" dataFormat={getNodeAction}>Action</TableHeaderColumn>
        </BootstrapTable>
      </YBPanelItem>
    );
  }
}
