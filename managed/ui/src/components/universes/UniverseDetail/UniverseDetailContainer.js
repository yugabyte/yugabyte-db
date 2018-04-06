// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { UniverseDetail } from '../../universes';
import { fetchUniverseInfo, fetchUniverseInfoResponse, resetUniverseInfo, fetchUniverseTasks,
  fetchUniverseTasksResponse, resetUniverseTasks, openDialog, closeDialog, getHealthCheck,
  getHealthCheckResponse
} from '../../../actions/universe';

import { fetchUniverseTables, fetchUniverseTablesSuccess, fetchUniverseTablesFailure,
  resetTablesList } from '../../../actions/tables';

const mapDispatchToProps = (dispatch) => {
  return {
    getUniverseInfo: (uuid) => {
      dispatch(fetchUniverseInfo(uuid))
      .then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    },

    fetchUniverseTables: (universeUUID) => {
      dispatch(fetchUniverseTables(universeUUID)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(fetchUniverseTablesFailure(response.payload));
        } else {
          dispatch(fetchUniverseTablesSuccess(response.payload));
        }
      });
    },

    resetTablesList: () => {
      dispatch(resetTablesList());
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
    },
    getHealthCheck: (uuid) => {
      dispatch(getHealthCheck(uuid))
      .then((response) => {
        dispatch(getHealthCheckResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe,
    providers: state.cloud.providers
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDetail);
