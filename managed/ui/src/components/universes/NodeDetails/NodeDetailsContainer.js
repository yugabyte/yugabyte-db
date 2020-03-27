// Copyright YugaByte Inc.

import { NodeDetails } from '../../universes';
import { connect } from 'react-redux';
import { getUniversePerNodeStatus, getUniversePerNodeStatusResponse,
  getUniversePerNodeMetrics, getUniversePerNodeMetricsResponse,
  getMasterLeader, getMasterLeaderResponse, resetMasterLeader } from '../../../actions/universe';

function mapStateToProps(state) {
  return {
    universe: state.universe,
    customer: state.customer
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    getMasterLeader: (uuid) => {
      dispatch(getMasterLeader(uuid)).then((response) => {
        dispatch(getMasterLeaderResponse(response.payload));
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
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(NodeDetails);
