// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { UniverseDetail } from '../../universes';
import { fetchUniverseInfo, fetchUniverseInfoResponse, resetUniverseInfo, fetchUniverseTasks,
  fetchUniverseTasksResponse, resetUniverseTasks, closeUniverseDialog, getHealthCheck,
  getHealthCheckResponse
} from '../../../actions/universe';

import { openDialog, closeDialog } from '../../../actions/modal';

import { fetchUniverseTables, fetchUniverseTablesSuccess, fetchUniverseTablesFailure,
  resetTablesList } from '../../../actions/tables';
import { getPrimaryCluster } from "../../../utils/UniverseUtils";
import { isDefinedNotNull, isNonEmptyObject }
  from '../../../utils/ObjectUtils';

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
      dispatch(closeUniverseDialog());
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
  // detect if software update is available for this universe  
  const isUpdateAvailable = (state) => {
    try {
      if(isDefinedNotNull(state.universe.currentUniverse.data) && isNonEmptyObject(state.universe.currentUniverse.data)) {
        const primaryCluster = getPrimaryCluster(state.universe.currentUniverse.data.universeDetails.clusters);
        const currentversion = primaryCluster && (primaryCluster.userIntent.ybSoftwareVersion || undefined);
        if (currentversion) {
          for (let idx = 0; idx < state.customer.softwareVersions.length; idx++) {
            const current = currentversion.split("-");
            const iterator = state.customer.softwareVersions[idx].split("-");
            // return number of versions avail to upgrade
            if ( iterator[0] < current[0] //compare version and release codes separately because "b9" > "b13"
              || (iterator[0] === current[0] && parseInt(iterator[1].substr(1), 10) <= parseInt(current[1].substr(1), 10))) return idx;
          };
        }
      }
      return false;
    } catch (err) {
      console.log("Versions comparison failed with: "+err);
      return false;
    }
  };

  return {
    universe: state.universe,
    modal: state.modal,
    providers: state.cloud.providers,
    updateAvailable: isUpdateAvailable(state)
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDetail);
