// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBPanelItem } from '../../panels';
import {
  isValidObject, isNonEmptyArray, isDefinedNotNull, insertSpacesFromCamelCase, isNonEmptyObject
} from 'utils/ObjectUtils';
import NodeConnectModal from './NodeConnectModal';
import { isNodeRemovable } from 'utils/UniverseUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';

export default class NodeDetails extends Component {
  constructor(props) {
    super(props);
    this.deleteNode = this.deleteNode.bind(this);
    this.isNodeStatusLoadingOrInit = this.isNodeStatusLoadingOrInit.bind(this);
    this.checkTasksForUniverseCreated = this.checkTasksForUniverseCreated.bind(this);
    this.state = { universeCreated: false };
  }

  componentWillReceiveProps(nextProps) {
    const { universe: { universeMasterLeader }, uuid } = this.props;
    if (isValidObject(this.refs.nodeDetailTable)) {
      this.refs.nodeDetailTable.handleSort('asc', 'name');
    }
    if (this.isNodeStatusLoadingOrInit() && !this.state.universeCreated) {
      const universeCreated = this.checkTasksForUniverseCreated();
      this.setState({universeCreated: universeCreated});
      if (getPromiseState(universeMasterLeader).isInit() && universeCreated) {
        this.props.getMasterLeader(uuid);
      }
    }
  }

  componentWillMount() {
    const { universeSelectionId, universeUUID } = this.props;
    const uuid = isDefinedNotNull(universeSelectionId) ? universeUUID : this.props.uuid;
    this.props.getUniversePerNodeStatus(uuid);
  }

  isNodeStatusLoadingOrInit() {
    const { universe: { universePerNodeStatus } } = this.props;
    const promiseState = getPromiseState(universePerNodeStatus);
    return (promiseState.isLoading() || promiseState.isInit());
  }

  checkTasksForUniverseCreated() {
    const { universe: { universeTasks }, uuid } = this.props;
    return isNonEmptyArray(universeTasks.data[uuid]) ? universeTasks.data[uuid].some((task) => {
      return task.type === 'Create' && task.target === 'Universe' && task.status === 'Success';
    }) : false;
  }

  deleteNode(node) {
    const {universe: {currentUniverse}} = this.props;
    const universeUUID = currentUniverse.data.universeUUID;
    this.props.deleteNode(node.name, universeUUID);
  }

  render() {
    const { universe: {currentUniverse, universePerNodeStatus, universeMasterLeader}} = this.props;
    const self = this;
    const nodeDetails = currentUniverse.data.universeDetails.nodeDetailsSet;
    if (!isNonEmptyArray(nodeDetails)) {
      return <span />;
    }

    const inLoadingOrInitState = this.isNodeStatusLoadingOrInit();
    const loadingIcon = <i className="fa fa-spinner fa-spin" />;
    const successIcon = <i className="fa fa-check-circle yb-success-color" />;
    const warningIcon = <i className="fa fa-warning yb-fail-color" />;

    const nodeDetailRows = nodeDetails.map((nodeDetail) => {
      let nodeStatus = "-";

      if (!inLoadingOrInitState && isNonEmptyObject(universePerNodeStatus.data)
          && isNonEmptyObject(universePerNodeStatus.data[nodeDetail.nodeName])) {
        nodeStatus = insertSpacesFromCamelCase(universePerNodeStatus.data[nodeDetail.nodeName]["node_status"]);
      }
      return {
        name: nodeDetail.nodeName,
        regionAz: `${nodeDetail.cloudInfo.region}/${nodeDetail.cloudInfo.az}`,
        isMaster: nodeDetail.isMaster ? "Details" : "-",
        masterPort: nodeDetail.masterHttpPort,
        tserverPort: nodeDetail.tserverHttpPort,
        isTServer: nodeDetail.isTserver ? "Details" : "-",
        privateIP: nodeDetail.cloudInfo.private_ip,
        publicIP: nodeDetail.cloudInfo.public_ip,
        nodeStatus: nodeStatus,
        cloudInfo: nodeDetail.cloudInfo
      };
    });

    const formatIpPort = function(cell, row, type) {
      if (cell === "-") {
        return <span>{cell}</span>;
      }
      const isMaster = type === "master";
      if (isMaster && isDefinedNotNull(universeMasterLeader) && getPromiseState(universeMasterLeader).isSuccess()
          && universeMasterLeader.data.privateIP === row.privateIP) {
        cell += " (Leader)";
      }
      const href = "http://" + row.privateIP + ":" + (isMaster ? row.masterPort : row.tserverPort);
      let statusDisplay = (!self.state.universeCreated || inLoadingOrInitState) ? loadingIcon : warningIcon;
      if (self.state.universeCreated && isDefinedNotNull(currentUniverse.data) && !inLoadingOrInitState) {
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

    const nodeIPs = nodeDetailRows
      .filter((node) => isDefinedNotNull(node.privateIP) && isDefinedNotNull(node.publicIP))
      .map((node) => ({ privateIP: node.privateIP, publicIP: node.publicIP }));

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
        { nodeIPs && <NodeConnectModal nodeIPs={nodeIPs} providerUUID={currentUniverse.data.provider.uuid} />}
        <BootstrapTable ref='nodeDetailTable' data={nodeDetailRows} >
          <TableHeaderColumn dataField="name" isKey={true} dataFormat={getNodeNameLink}>Name</TableHeaderColumn>
          <TableHeaderColumn dataField="regionAz">Region/Zone</TableHeaderColumn>
          <TableHeaderColumn dataField="isMaster" dataFormat={ formatIpPort } formatExtraData="master" >
            Master
          </TableHeaderColumn>
          <TableHeaderColumn dataField="isTServer" dataFormat={ formatIpPort } formatExtraData="tserver" >
            TServer
          </TableHeaderColumn>
          <TableHeaderColumn dataField="privateIP">Private IP</TableHeaderColumn>
          <TableHeaderColumn dataField="nodeStatus">Status</TableHeaderColumn>
          <TableHeaderColumn dataField="nodeAction" dataFormat={getNodeAction}>Action</TableHeaderColumn>
        </BootstrapTable>
      </YBPanelItem>
    );
  }
}
