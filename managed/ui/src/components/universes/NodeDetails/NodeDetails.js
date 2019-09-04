// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import axios from 'axios';
import { NodeDetailsTable } from '../../universes';
import { isNonEmptyArray, isDefinedNotNull, insertSpacesFromCamelCase, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { getPrimaryCluster, getReadOnlyCluster, nodeComparisonFunction } from '../../../utils/UniverseUtils';
import { hasLiveNodes } from 'utils/UniverseUtils';
import { YBLoading } from '../../common/indicators';

export default class NodeDetails extends Component {
  constructor(props) {
    super(props);
    this.state = {
      tabletServers: null,
    };
  }

  componentDidMount() {
    const { universe: { currentUniverse } } = this.props;
    if (getPromiseState(currentUniverse).isSuccess() &&
        hasLiveNodes(currentUniverse.data)) {
      const uuid = currentUniverse.data.universeUUID;
      this.props.getUniversePerNodeStatus(uuid);
      this.props.getMasterLeader(uuid);
    }
  }

  componentDidUpdate(prevProps, prevState) {
    const { universe: { currentUniverse, universeMasterLeader }} = this.props;
    const universeDetails = currentUniverse.data.universeDetails;
    const nodeDetails = universeDetails.nodeDetailsSet;
    const self = this;
    if (getPromiseState(currentUniverse).isSuccess() && getPromiseState(universeMasterLeader).isSuccess() && !isDefinedNotNull(this.state.tabletServers)) {
      const httpPort = nodeDetails.find(item => item.cloudInfo.private_ip === universeMasterLeader.data.privateIP).masterHttpPort;
      self.setState({
        tabletServers: {}
      });
      const axiosInstance = axios.create({
        baseURL: `http://${universeMasterLeader.data.privateIP}:${httpPort}/api/v1/tablet-servers`,
        timeout: 3000,
        headers: {
          'Content-Type': 'text/plain',
          'Accept': '*/*'
        }
      });
      delete axiosInstance.defaults.headers.common['X-AUTH-TOKEN'];
      axiosInstance
        .get(`http://${universeMasterLeader.data.privateIP}:${httpPort}/api/v1/tablet-servers`)
        .then(response => {
          self.setState({
            tabletServers: response.data
          });
        });
    }
  }

  render() {
    const { universe: { currentUniverse, universePerNodeStatus, universeMasterLeader }} = this.props;
    const universeDetails = currentUniverse.data.universeDetails;
    const nodeDetails = universeDetails.nodeDetailsSet;
    if (!isNonEmptyArray(nodeDetails) || !isDefinedNotNull(this.state.tabletServers) || !isNonEmptyObject(this.state.tabletServers)) {
      return <YBLoading />;
    }
    const isReadOnlyUniverse = getPromiseState(currentUniverse).isSuccess() && currentUniverse.data.universeDetails.capability === "READ_ONLY";

    const universeCreated = universeDetails.updateInProgress;
    const sortedNodeDetails = nodeDetails.sort((a, b) => nodeComparisonFunction(a, b, currentUniverse.data.universeDetails.clusters));

    const nodesMetrics = this.state.tabletServers[Object.keys(this.state.tabletServers)[0]];
    const nodeDetailRows = sortedNodeDetails.map((nodeDetail) => {
      let nodeStatus = "-";
      let nodeAlive = false;
      let isLoading = universeCreated;
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
      const metricsData = nodesMetrics[`${nodeDetail.cloudInfo.private_ip}:${nodeDetail.tserverHttpPort}`];
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
        placementUUID: nodeDetail.placementUuid,
        ...metricsData
      };
    });

    const primaryCluster = getPrimaryCluster(universeDetails.clusters);
    if (!isNonEmptyObject(primaryCluster)) {
      return <span />;
    }
    const readOnlyCluster = getReadOnlyCluster(universeDetails.clusters);
    const primaryNodeDetails = nodeDetailRows.filter((nodeDetail) => nodeDetail.placementUUID === primaryCluster.uuid);
    const readOnlyNodeDetails = isNonEmptyObject(readOnlyCluster) ?
      nodeDetailRows.filter((nodeDetail) => nodeDetail.placementUUID === readOnlyCluster.uuid) : [];

    return (
      <Fragment>
        <NodeDetailsTable isKubernetesCluster={primaryCluster.userIntent.providerType === "kubernetes"} isReadOnlyUniverse={isReadOnlyUniverse} nodeDetails={primaryNodeDetails} providerUUID={primaryCluster.userIntent.provider} clusterType='primary' />
        { readOnlyCluster && <NodeDetailsTable isKubernetesCluster={readOnlyCluster.userIntent.providerType === "kubernetes"} isReadOnlyUniverse={isReadOnlyUniverse} nodeDetails={readOnlyNodeDetails} providerUUID={readOnlyCluster.userIntent.provider} clusterType='readonly' /> }
      </Fragment>
    );
  }
}
