// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBPanelItem } from '../../panels';
import NodeConnectModal from './NodeConnectModal';
import { YBLoadingIcon } from '../../common/indicators';
import { YBButton } from '../../common/forms/fields';
import { isNonEmptyArray, isDefinedNotNull, insertSpacesFromCamelCase, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { getPrimaryCluster, isNodeRemovable } from '../../../utils/UniverseUtils';
import { IN_DEVELOPMENT_MODE } from '../../../config';

export default class NodeDetails extends Component {
  constructor(props) {
    super(props);
    this.state = { universeCreated: false };
  }

  componentWillReceiveProps(nextProps) {
    const { universe: { universeMasterLeader }, uuid } = this.props;
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

  isNodeStatusLoadingOrInit = () => {
    const { universe: { universePerNodeStatus } } = this.props;
    const promiseState = getPromiseState(universePerNodeStatus);
    return (promiseState.isLoading() || promiseState.isInit());
  };

  checkTasksForUniverseCreated = () => {
    const { universe: { universeTasks }, uuid } = this.props;
    return isNonEmptyArray(universeTasks.data[uuid]) ? universeTasks.data[uuid].some((task) => {
      return task.type === 'Create' && task.target === 'Universe' && task.status === 'Success';
    }) : false;
  };

  performNodeAction(node, actionType) {
    const {universe: {currentUniverse}} = this.props;
    const universeUUID = currentUniverse.data.universeUUID;
    this.props.performUniverseNodeAction(universeUUID, node.name, actionType);
  }

  render() {
    const { universe: {currentUniverse, universePerNodeStatus, universeMasterLeader}} = this.props;
    const self = this;
    const nodeDetails = currentUniverse.data.universeDetails.nodeDetailsSet;
    if (!isNonEmptyArray(nodeDetails)) {
      return <span />;
    }

    const inLoadingOrInitState = this.isNodeStatusLoadingOrInit();
    const loadingIcon = <YBLoadingIcon size='inline' />;
    const successIcon = <i className="fa fa-check-circle yb-success-color" />;
    const warningIcon = <i className="fa fa-warning yb-fail-color" />;
    const sortedNodeDetails = nodeDetails.sort((a, b) => a.nodeIdx - b.nodeIdx);

    const nodeDetailRows = sortedNodeDetails.map((nodeDetail) => {
      let nodeStatus = "-";
      if (!inLoadingOrInitState && isNonEmptyObject(universePerNodeStatus.data)
        && isNonEmptyObject(universePerNodeStatus.data[nodeDetail.nodeName])) {
        nodeStatus = insertSpacesFromCamelCase(universePerNodeStatus.data[nodeDetail.nodeName]["node_status"]);
      }
      return {
        name: nodeDetail.nodeName,
        regionItem: `${nodeDetail.cloudInfo.region}`,
        azItem: `${nodeDetail.cloudInfo.az}`,
        isMaster: nodeDetail.isMaster ? "Details" : "-",
        masterPort: nodeDetail.masterHttpPort,
        tserverPort: nodeDetail.tserverHttpPort,
        isTServer: nodeDetail.isTserver ? "Details" : "-",
        privateIP: nodeDetail.cloudInfo.private_ip,
        publicIP: nodeDetail.cloudInfo.public_ip,
        nodeStatus: nodeStatus,
        allowedActions: nodeDetail.allowedActions,
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
      let href = "";
      if (IN_DEVELOPMENT_MODE) {
        href = "http://" + row.privateIP + ":" + (isMaster ? row.masterPort : row.tserverPort);
      } else {
        href = "/proxy/" + row.privateIP + ":" + (isMaster ? row.masterPort : row.tserverPort) + "/";
      }
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
      } else if (row.cloudInfo.cloud === "gcp") {
        const gcpURI = `https://console.cloud.google.com/compute/instancesDetail/zones/${row.azItem}/instances/${cell}`;
        return <a href={gcpURI} target="_blank" rel="noopener noreferrer">{cell}</a>;
      } else {
        return cell;
      }
    };

    const getNodeAction = function(cell, row, type) {
      if (isNodeRemovable(row.nodeStatus)) {
        return <YBButton btnText="Delete" onClick={self.performNodeAction.bind(self, row, 'DELETE')}/>;
      }

      if (true) {
        if (row.nodeStatus === "Live") {
          return <YBButton btnText="Stop" onClick={self.performNodeAction.bind(self, row, 'STOP')}/>;
        }
        if (row.nodeStatus === "Stopped") {
          return <YBButton btnText="Start" onClick={self.performNodeAction.bind(self, row, 'START')}/>;
        }
      } else {
        // TODO (Ram/Bharat): change the YBButton to YBSelect and use allowedActions as option, of course
        // the onChange would also now not pass any action. New states to be added are in comments...
        if (row.nodeStatus === "Live") {
          return <YBButton btnText="Remove" onClick={self.performNodeAction.bind(self, row, 'REMOVE')}/>; // Stop
        }
        if (row.nodeStatus === "Removed") {
          return <YBButton btnText="Release" onClick={self.performNodeAction.bind(self, row, 'RELEASE')}/>; // Add
        }
        if (row.nodeStatus === "Stopped") {
          return <YBButton btnText="Start" onClick={self.performNodeAction.bind(self, row, 'START')}/>; // Release
        }
        if (row.nodeStatus === "Decommissioned") {
          return <YBButton btnText="Add" onClick={self.performNodeAction.bind(self, row, 'ADD')}/>;
        }
      }
    };

    const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
    if (!isNonEmptyObject(primaryCluster)) {
      return <span />;
    }

    return (
      <YBPanelItem
        header={
          <div>
            <div className='pull-right'>
              { nodeIPs && <NodeConnectModal nodeIPs={nodeIPs} providerUUID={primaryCluster.userIntent.provider} />}
            </div>
            <h2 className='content-title'>Nodes</h2>
          </div>
        }
        body={
          <div>
            <BootstrapTable ref='nodeDetailTable' data={nodeDetailRows}>
              <TableHeaderColumn dataField="name" isKey={true} className={'node-name-field'} columnClassName={'node-name-field'}
                dataFormat={getNodeNameLink}>Name</TableHeaderColumn>
              <TableHeaderColumn dataField="regionItem">Region</TableHeaderColumn>
              <TableHeaderColumn dataField="azItem">Zone</TableHeaderColumn>
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
          </div>
        }
      />
    );
  }
}
