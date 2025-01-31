// Copyright YugaByte Inc.

import { NodeDetails } from '../../universes';
import { connect } from 'react-redux';
import {
  getUniversePerNodeStatus,
  getUniversePerNodeStatusResponse,
  getUniversePerNodeMetrics,
  getUniversePerNodeMetricsResponse,
  getMasterInfos,
  getMasterInfosResponse,
  resetMasterLeader,
  resetNodeDetails
} from '../../../actions/universe';
import {
  getNodeInstancesForProvider,
  getNodeInstancesForReadReplicaProvider,
  getNodesInstancesForProviderResponse,
  getNodesInstancesForReadReplicaProviderResponse
} from '../../../actions/cloud';

function mapStateToProps(state) {
  return {
    accessKeys: state.cloud.accessKeys,
    universe: state.universe,
    customer: state.customer,
    providers: state.cloud.providers
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    getMasterInfo: (uuid) => {
      dispatch(getMasterInfos(uuid)).then((response) => {
        dispatch(getMasterInfosResponse(response.payload));
      });
    },

    resetMasterLeader: () => {
      dispatch(resetMasterLeader());
    },

    /**
     * Get per-node status for a universe.
     *
     * uuid: UUID of the universe to get the per-node status of.
     */
    getUniversePerNodeStatus: (uuid) => {
      dispatch(getUniversePerNodeStatus(uuid)).then((perNodeResponse) => {
        dispatch(getUniversePerNodeStatusResponse(perNodeResponse.payload));
      });
    },

    getUniversePerNodeMetrics: (uuid) => {
      dispatch(getUniversePerNodeMetrics(uuid)).then((perNodeResponse) => {
        dispatch(getUniversePerNodeMetricsResponse(perNodeResponse.payload));
      });
    },

    fetchNodeListByProvider: (pUUID) => {
      dispatch(getNodeInstancesForProvider(pUUID)).then((response) => {
        dispatch(getNodesInstancesForProviderResponse(response.payload));
      });
    },

    resetNodeDetails: () => {
      dispatch(resetNodeDetails());
    },

    fetchNodeListByReplicaProvider: (pUUID) => {
      dispatch(getNodeInstancesForReadReplicaProvider(pUUID)).then((response) => {
        dispatch(getNodesInstancesForReadReplicaProviderResponse(response.payload));
      });
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(NodeDetails);
