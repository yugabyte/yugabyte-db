// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { UniverseDetailNew } from '../../universes';
import { fetchUniverseInfo, fetchUniverseInfoResponse, resetUniverseInfo, fetchUniverseTasks,
  fetchUniverseTasksResponse, resetUniverseTasks, openDialog, closeDialog, getUniversePerNodeStatus,
  getUniversePerNodeStatusResponse, getMasterLeader, getMasterLeaderResponse, resetMasterLeader
} from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    getUniverseInfo: (uuid) => {
      dispatch(fetchUniverseInfo(uuid))
      .then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    },

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

    resetUniverseInfo: () => {
      dispatch(resetUniverseInfo());
    },
    fetchUniverseTasks: (uuid) => {
      dispatch(fetchUniverseTasks(uuid))
      .then((response) => {
        dispatch(fetchUniverseTasksResponse(response.payload));
      });
    },
    resetUniverseTasks: () => {
      dispatch(resetUniverseTasks());
    },
    showUniverseModal: () => {
      dispatch(openDialog("universeModal"));
    },
    showGFlagsModal: () => {
      dispatch(openDialog("gFlagsModal"));
    },
    showDeleteUniverseModal: () => {
      dispatch(openDialog("deleteUniverseModal"));
    },
    showSoftwareUpgradesModal: () => {
      dispatch(openDialog("softwareUpgradesModal"));
    },
    closeModal: () => {
      dispatch(closeDialog());
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe,
    providers: state.cloud.providers
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDetailNew);
