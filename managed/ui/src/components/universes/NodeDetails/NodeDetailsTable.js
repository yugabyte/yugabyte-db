// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBLoadingIcon } from '../../common/indicators';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import { isDefinedNotNull } from 'utils/ObjectUtils';
import { YBPanelItem } from '../../panels';
import { NodeAction, NodeConnectModal } from '../../universes';


export default class NodeDetailsTable extends Component {
  render() {
    const { nodeDetails, providerUUID, clusterType } = this.props;
    const loadingIcon = <YBLoadingIcon size='inline' />;
    const successIcon = <i className="fa fa-check-circle yb-success-color" />;
    const warningIcon = <i className="fa fa-warning yb-fail-color" />;
    const sortedNodeDetails = nodeDetails.sort((a, b) => a.nodeIdx - b.nodeIdx);

    const nodeIPs = nodeDetails
      .filter((node) => isDefinedNotNull(node.privateIP) && isDefinedNotNull(node.publicIP))
      .map((node) => ({ privateIP: node.privateIP, publicIP: node.publicIP }));

    const formatIpPort = function(cell, row, type) {
      if (cell === "-") {
        return <span>{cell}</span>;
      }
      const isMaster = type === "master";
      if (isMaster && row.isMasterLeader) {
        cell += " (Leader)";
      }
      let href = "";
      if (IN_DEVELOPMENT_MODE) {
        href = "http://" + row.privateIP + ":" + (isMaster ? row.masterPort : row.tserverPort);
      } else {
        href = "/proxy/" + row.privateIP + ":" + (isMaster ? row.masterPort : row.tserverPort) + "/";
      }

      if (row.nodeAlive) {
        return <span>{successIcon}&nbsp;<a href={href} target="_blank" rel="noopener noreferrer">{cell}</a></span>;
      } else {
        return <span>{row.isLoading ? loadingIcon : warningIcon}&nbsp;{cell}</span>;
      }
    };

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
      return (
        <NodeAction currentRow={row} />
      );
    };

    const panelTitle = clusterType === 'primary' ? 'Primary Cluster': 'Read Replicas';
    return (
      <YBPanelItem
        className={`${clusterType}-node-details`}
        header={
          <div>
            <div className='pull-right'>
              { nodeIPs && <NodeConnectModal nodeIPs={nodeIPs} providerUUID={providerUUID} />}
            </div>
            <h2 className='content-title'>{panelTitle}</h2>
          </div>
        }
        body={
          <BootstrapTable ref='nodeDetailTable' data={sortedNodeDetails}>
            <TableHeaderColumn dataField="name" isKey={true} className={'node-name-field'}
                               columnClassName={'node-name-field'}
              dataFormat={getNodeNameLink}>Name</TableHeaderColumn>
            <TableHeaderColumn dataField="cloudItem">Cloud</TableHeaderColumn>
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
            <TableHeaderColumn dataField="nodeAction"  columnClassName={"yb-actions-cell"}
              dataFormat={getNodeAction}>Action</TableHeaderColumn>
          </BootstrapTable>
        }
      />
    );
  }
}
