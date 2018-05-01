// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { NodeDetailsTable } from '../../universes';
import { isNonEmptyArray, isDefinedNotNull, insertSpacesFromCamelCase, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { getPrimaryCluster, getReadOnlyCluster } from '../../../utils/UniverseUtils';

export default class NodeDetails extends Component {
  componentWillMount() {
    const { universe: { currentUniverse } } = this.props;
    const uuid = currentUniverse.data.universeUUID;
    this.props.getUniversePerNodeStatus(uuid);
    this.props.getMasterLeader(uuid);
  }

  checkTasksForUniverseCreated = () => {
    const { universe: { currentUniverse, universeTasks } } = this.props;
    const uuid = currentUniverse.data.universeUUID;
    return isNonEmptyArray(universeTasks.data[uuid]) ? universeTasks.data[uuid].some((task) => {
      return task.type === 'Create' && task.target === 'Universe' && task.status === 'Success';
    }) : false;
  };

  render() {
    const { universe: { currentUniverse, universePerNodeStatus, universeMasterLeader } } = this.props;
    const nodeDetails = currentUniverse.data.universeDetails.nodeDetailsSet;
    if (!isNonEmptyArray(nodeDetails)) {
      return <span />;
    }

    const universeCreated = this.checkTasksForUniverseCreated();
    const nodeDetailRows = nodeDetails.map((nodeDetail) => {
      let nodeStatus = "-";
      let nodeAlive = false;
      let isLoading = !universeCreated;
      if (getPromiseState(universePerNodeStatus).isSuccess() &&
          isNonEmptyObject(universePerNodeStatus.data) &&
          isNonEmptyObject(universePerNodeStatus.data[nodeDetail.nodeName])) {
        nodeStatus = insertSpacesFromCamelCase(universePerNodeStatus.data[nodeDetail.nodeName]["node_status"]);
        nodeAlive = universePerNodeStatus.data[nodeDetail.nodeName][nodeDetail.isMaster ? "master_alive" : "tserver_alive"];
        isLoading = false;
      }
      const isMasterLeader = nodeDetail.isMaster && isDefinedNotNull(universeMasterLeader) &&
                             getPromiseState(universeMasterLeader).isSuccess() &&
                             universeMasterLeader.data.privateIP === nodeDetail.cloudInfo.private_ip;

      return {
        nodeIdx: nodeDetail.nodeIdx,
        name: nodeDetail.nodeName,
        cloudItem: `${nodeDetail.cloudInfo.cloud}`,
        regionItem: `${nodeDetail.cloudInfo.region}`,
        azItem: `${nodeDetail.cloudInfo.az}`,
        isMaster: nodeDetail.isMaster ? "Details" : "-",
        isMasterLeader: isMasterLeader,
        masterPort: nodeDetail.masterHttpPort,
        tserverPort: nodeDetail.tserverHttpPort,
        isTServer: nodeDetail.isTserver ? "Details" : "-",
        privateIP: nodeDetail.cloudInfo.private_ip,
        publicIP: nodeDetail.cloudInfo.public_ip,
        nodeStatus: nodeStatus,
        allowedActions: nodeDetail.allowedActions,
        cloudInfo: nodeDetail.cloudInfo,
        isLoading: isLoading,
        nodeAlive: nodeAlive,
        placementUUID: nodeDetail.placementUuid
      };
    });

    const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
    if (!isNonEmptyObject(primaryCluster)) {
      return <span />;
    }
    const readOnlyCluster = getReadOnlyCluster(currentUniverse.data.universeDetails.clusters);
    const primaryNodeDetails = nodeDetailRows.filter((nodeDetail) => nodeDetail.placementUUID === primaryCluster.uuid);
    const readOnlyNodeDetails = isNonEmptyObject(readOnlyCluster) ?
      nodeDetailRows.filter((nodeDetail) => nodeDetail.placementUUID === readOnlyCluster.uuid) : [];

    return (
      <Fragment>
        <NodeDetailsTable nodeDetails={primaryNodeDetails} providerUUID={primaryCluster.userIntent.provider} clusterType='primary' />
        { readOnlyCluster && <NodeDetailsTable nodeDetails={readOnlyNodeDetails} providerUUID={readOnlyCluster.userIntent.provider} clusterType='readonly' /> }
      </Fragment>
    );
  }
}
