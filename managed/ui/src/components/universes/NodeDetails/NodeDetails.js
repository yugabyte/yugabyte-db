// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import _ from 'lodash';

import { NodeDetailsTable } from '../../universes';
import {
  isNonEmptyArray,
  isDefinedNotNull,
  insertSpacesFromCamelCase,
  isNonEmptyObject,
  isNonEmptyString
} from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import {
  getPrimaryCluster,
  getReadOnlyCluster,
  nodeComparisonFunction,
  hasLiveNodes
} from '../../../utils/UniverseUtils';

import { YBLoading } from '../../common/indicators';

export default class NodeDetails extends Component {
  componentDidMount() {
    const {
      universe: { currentUniverse }
    } = this.props;
    if (getPromiseState(currentUniverse).isSuccess()) {
      const uuid = currentUniverse.data.universeUUID;
      this.props.getUniversePerNodeStatus(uuid);
      this.props.resetNodeDetails();
      this.props.getMasterNodesInfo(uuid);
      if (hasLiveNodes(currentUniverse.data)) {
        this.props.getUniversePerNodeMetrics(uuid);
      }

      const universeDetails = currentUniverse.data.universeDetails;
      const primaryCluster = getPrimaryCluster(universeDetails.clusters);
      if (isDefinedNotNull(primaryCluster)) {
        const primaryClusterProvider = primaryCluster.userIntent.provider;
        this.props.fetchNodeListByProvider(primaryClusterProvider);
      }

      const readOnlyCluster = getReadOnlyCluster(universeDetails.clusters);
      if (isDefinedNotNull(readOnlyCluster)) {
        const readOnlyClusterProvider = readOnlyCluster.userIntent.provider;
        this.props.fetchNodeListByReplicaProvider(readOnlyClusterProvider);
      }
    }
  }

  componentWillUnmount() {
    this.props.resetMasterLeader();
  }

  render() {
    const {
      universe: {
        currentUniverse,
        nodeInstanceList,
        replicaNodeInstanceList,
        universePerNodeStatus,
        universeNodeDetails,
        universePerNodeMetrics,
        universeMasterNodes
      },
      customer,
      providers
    } = this.props;
    const universeDetails = currentUniverse.data.universeDetails;
    const nodeDetails = universeDetails.nodeDetailsSet;
    if (!isNonEmptyArray(nodeDetails)) {
      return <YBLoading />;
    }
    const isReadOnlyUniverse =
      getPromiseState(currentUniverse).isSuccess() &&
      currentUniverse.data.universeDetails.capability === 'READ_ONLY';
    const universeCreated = universeDetails.updateInProgress;
    const sortedNodeDetails = nodeDetails.sort((a, b) =>
      nodeComparisonFunction(a, b, currentUniverse.data.universeDetails.clusters)
    );

    const nodesMetrics =
      getPromiseState(universePerNodeMetrics).isSuccess() &&
      isNonEmptyObject(universePerNodeMetrics.data) &&
      Object.assign({}, ...Object.values(universePerNodeMetrics.data));

    const nodeDetailRows = sortedNodeDetails.map((nodeDetail) => {
      let nodeStatus = '-';
      let masterAlive = false;
      let tserverAlive = false;
      let isLoading = universeCreated;
      const privateIP = nodeDetail.cloudInfo.private_ip;
      const hasMasterNodes =
        getPromiseState(universeMasterNodes).isSuccess() &&
        isNonEmptyArray(universeMasterNodes.data);
      const masterNode =
        hasMasterNodes &&
        universeMasterNodes.data?.filter((masterNode) => masterNode.host === privateIP);
      const masterUUID = masterNode?.[0]?.masterUUID;
      const isMasterLeader = masterNode?.[0]?.isLeader;

      let allowedNodeActions = nodeDetail.allowedActions;
      const nodeName = nodeDetail.nodeName;
      const hasUniverseNodeDetails =
        getPromiseState(universeNodeDetails).isSuccess() &&
        isNonEmptyObject(universeNodeDetails.data);

      // When node operation is in progress and when user swicthes between different tabs,
      // polling stops and when user comes back to the nodes tab, this gives current status
      // of all the nodes during mount
      if (
        getPromiseState(universePerNodeStatus).isSuccess() &&
        isNonEmptyObject(universePerNodeStatus.data) &&
        isNonEmptyObject(universePerNodeStatus.data[nodeDetail.nodeName])
      ) {
        nodeStatus = insertSpacesFromCamelCase(
          universePerNodeStatus.data[nodeDetail.nodeName]['node_status']
        );
        masterAlive = universePerNodeStatus.data[nodeDetail.nodeName]['master_alive'];
        tserverAlive = universePerNodeStatus.data[nodeDetail.nodeName]['tserver_alive'];
      }

      if (hasUniverseNodeDetails && nodeName === universeNodeDetails.data.nodeName) {
        allowedNodeActions = universeNodeDetails.data.allowedActions;
        masterAlive = universeNodeDetails.data.isMaster;
        tserverAlive = universePerNodeStatus.data.isTserver;
        nodeStatus = insertSpacesFromCamelCase(universeNodeDetails.data.state);
        isLoading = false;
      }

      let instanceName = '';

      if (isDefinedNotNull(nodeInstanceList)) {
        const matchingInstance = nodeInstanceList.data?.filter(
          (instance) => instance.nodeName === nodeName
        );
        instanceName = _.get(matchingInstance, '[0]details.instanceName', '');
      }

      if (!isNonEmptyString(instanceName) && isDefinedNotNull(replicaNodeInstanceList)) {
        const matchingInstance = replicaNodeInstanceList.data.filter(
          (instance) => instance.nodeName === nodeName
        );
        instanceName = _.get(matchingInstance, '[0]details.instanceName', '');
      }

      const metricsData = nodesMetrics
        ? nodesMetrics[`${nodeDetail.cloudInfo.private_ip}:${nodeDetail.tserverHttpPort}`]
        : {
            active_tablets: null,
            num_sst_files: null,
            ram_used: null,
            read_ops_per_sec: null,
            system_tablets_leaders: null,
            system_tablets_total: null,
            time_since_hb: null,
            total_sst_file_size: null,
            uncompressed_sst_file_size: null,
            uptime_seconds: null,
            user_tablets_leaders: null,
            user_tablets_total: null,
            write_ops_per_sec: null,
            uuid: null
          };
      return {
        nodeIdx: nodeDetail.nodeIdx,
        nodeUuid: nodeDetail.nodeUuid,
        name: nodeName,
        instanceName: instanceName,
        cloudItem: `${nodeDetail.cloudInfo.cloud}`,
        regionItem: `${nodeDetail.cloudInfo.region}`,
        azItem: `${nodeDetail.cloudInfo.az}`,
        isMaster: nodeDetail.isMaster ? 'Details' : '-',
        isMasterProcess: nodeDetail.isMaster,
        isMasterLeader: isMasterLeader,
        masterPort: nodeDetail.masterHttpPort,
        tserverPort: nodeDetail.tserverHttpPort,
        isTServer: nodeDetail.isTserver ? 'Details' : '-',
        isTServerProcess: nodeDetail.isTserver,
        privateIP: nodeDetail.cloudInfo.private_ip,
        publicIP: nodeDetail.cloudInfo.public_ip,
        nodeStatus,
        allowedActions: allowedNodeActions,
        cloudInfo: nodeDetail.cloudInfo,
        isLoading: isLoading,
        isMasterAlive: masterAlive,
        isTserverAlive: tserverAlive,
        placementUUID: nodeDetail.placementUuid,
        dedicatedTo: nodeDetail.dedicatedTo,
        masterUUID,
        ...metricsData
      };
    });

    const primaryCluster = getPrimaryCluster(universeDetails.clusters);
    if (!isNonEmptyObject(primaryCluster)) {
      return <span />;
    }
    const readOnlyCluster = getReadOnlyCluster(universeDetails.clusters);
    const primaryNodeDetails = nodeDetailRows.filter(
      (nodeDetail) => nodeDetail.placementUUID === primaryCluster.uuid
    );
    const readOnlyNodeDetails = isNonEmptyObject(readOnlyCluster)
      ? nodeDetailRows.filter((nodeDetail) => nodeDetail.placementUUID === readOnlyCluster.uuid)
      : [];

    return (
      <Fragment>
        <NodeDetailsTable
          isKubernetesCluster={primaryCluster.userIntent.providerType === 'kubernetes'}
          isReadOnlyUniverse={isReadOnlyUniverse}
          nodeDetails={primaryNodeDetails}
          providerUUID={primaryCluster.userIntent.provider}
          clusterType="primary"
          isDedicatedNodes={primaryCluster.userIntent.dedicatedNodes}
          customer={customer}
          currentUniverse={currentUniverse}
          providers={providers}
        />
        {readOnlyCluster && (
          <NodeDetailsTable
            isKubernetesCluster={readOnlyCluster.userIntent.providerType === 'kubernetes'}
            isReadOnlyUniverse={isReadOnlyUniverse}
            nodeDetails={readOnlyNodeDetails}
            providerUUID={readOnlyCluster.userIntent.provider}
            clusterType="readonly"
            customer={customer}
            currentUniverse={currentUniverse}
            providers={providers}
          />
        )}
      </Fragment>
    );
  }
}
