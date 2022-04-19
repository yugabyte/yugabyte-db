// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { UniverseDetail } from '../../universes';
import {
  fetchUniverseInfo,
  fetchUniverseInfoResponse,
  resetUniverseInfo,
  getHealthCheck,
  getHealthCheckResponse,
  updateBackupState,
  updateBackupStateResponse
} from '../../../actions/universe';
import {
  abortTask,
  abortTaskResponse,
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../actions/tasks';

import { getAlerts, getAlertsSuccess, getAlertsFailure } from '../../../actions/customers';

import { openDialog, closeDialog } from '../../../actions/modal';

import {
  fetchUniverseTables,
  fetchUniverseTablesSuccess,
  fetchUniverseTablesFailure,
  resetTablesList
} from '../../../actions/tables';
import { getPrimaryCluster } from '../../../utils/UniverseUtils';
import { isDefinedNotNull, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { toast } from 'react-toastify';

const mapDispatchToProps = (dispatch) => {
  return {
    getUniverseInfo: (uuid) => {
      dispatch(fetchUniverseInfo(uuid)).then((response) => {
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
    showUniverseModal: () => {
      dispatch(openDialog('universeModal'));
    },
    showGFlagsModal: () => {
      dispatch(openDialog('gFlagsModal'));
    },
    showManageKeyModal: () => {
      dispatch(openDialog('manageKeyModal'));
    },
    showDeleteUniverseModal: () => {
      dispatch(openDialog('deleteUniverseModal'));
    },
    showToggleUniverseStateModal: () => {
      dispatch(openDialog('toggleUniverseStateForm'));
    },
    showSoftwareUpgradesModal: () => {
      dispatch(openDialog('softwareUpgradesModal'));
    },
    showRunSampleAppsModal: () => {
      dispatch(openDialog('runSampleAppsModal'));
    },
    showTLSConfigurationModal: () => {
      dispatch(openDialog('tlsConfigurationModal'));
    },
    showRollingRestartModal: () => {
      dispatch(openDialog('rollingRestart'));
    },
    showUpgradeSystemdModal: () => {
      dispatch(openDialog('systemdUpgrade'));
    },
    showToggleBackupModal: () => {
      dispatch(openDialog('toggleBackupModalForm'));
    },
    updateBackupState: (universeUUID, flag) => {
      dispatch(updateBackupState(universeUUID, flag)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
        } else {
          toast.success('Successfully Enabled the backups.');
        }
        dispatch(updateBackupStateResponse(response.payload));
        dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
          dispatch(fetchUniverseInfoResponse(response.payload));
        });
      });
    },
    closeModal: () => {
      dispatch(closeDialog());
    },
    getHealthCheck: (uuid) => {
      dispatch(getHealthCheck(uuid)).then((response) => {
        dispatch(getHealthCheckResponse(response.payload));
      });
    },

    fetchCustomerTasks: () => {
      return dispatch(fetchCustomerTasks()).then((response) => {
        if (!response.error) {
          return dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          return dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    },
    getAlertsList: () => {
      dispatch(getAlerts()).then((response) => {
        if (response.payload.status === 200) {
          dispatch(getAlertsSuccess(response.payload));
        } else {
          dispatch(getAlertsFailure(response.payload));
        }
      });
    },
    abortCurrentTask: (taskUUID) => {
      return dispatch(abortTask(taskUUID)).then((response) => {
        return dispatch(abortTaskResponse(response.payload));
      });
    },
    hideTaskAbortModal: () => {
      dispatch(closeDialog());
    },
    showTaskAbortModal: () => {
      dispatch(openDialog('confirmAbortTask'));
    }
  };
};

function mapStateToProps(state, ownProps) {
  // Detect if software update is available for this universe
  const isUpdateAvailable = (state) => {
    const isFirstVersionOlder = (first, second) => {
      for (let idx = 0; idx < first.length; idx++) {
        const first_ = parseInt(first[idx], 10);
        const second_ = parseInt(second[idx], 10);
        if (first_ < second_) {
          return true;
        } else if (first_ > second_) {
          return false;
        }
      }
      return false;
    };
    try {
      if (
        isDefinedNotNull(state.universe.currentUniverse.data) &&
        isNonEmptyObject(state.universe.currentUniverse.data)
      ) {
        const primaryCluster = getPrimaryCluster(
          state.universe.currentUniverse.data.universeDetails.clusters
        );
        const currentversion =
          primaryCluster && (primaryCluster.userIntent.ybSoftwareVersion || undefined);
        if (currentversion && state.customer.softwareVersions.length) {
          for (let idx = 0; idx < state.customer.softwareVersions.length; idx++) {
            const current = currentversion.split('-');
            const iterator = state.customer.softwareVersions[idx].split('-');

            const currentVersion = current[0];
            const iteratorVersion = iterator[0];
            const currentBuild = current[1] ? parseInt(current[1].substr(1), 10) : '';
            const iteratorBuild = iterator[1] ? parseInt(iterator[1].substr(1), 10) : '';

            // Compare versions till current won't be founded or founded an older one and compare release codes separately because "b9" > "b13"
            if (
              isFirstVersionOlder(iteratorVersion.split('.'), currentVersion.split('.')) ||
              (iteratorVersion === currentVersion && iteratorBuild <= currentBuild)
            )
              return idx;
          }
        }
      }
      return false;
    } catch (err) {
      console.log('Versions comparison failed with: ' + err);
      return false;
    }
  };

  return {
    customer: state.customer,
    universe: state.universe,
    tasks: state.tasks,
    universeTables: state.tables.universeTablesList,
    modal: state.modal,
    providers: state.cloud.providers,
    updateAvailable: isUpdateAvailable(state),
    featureFlags: state.featureFlags,
    accessKeys: state.cloud.accessKeys
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDetail);
