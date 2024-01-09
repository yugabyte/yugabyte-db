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
  updateBackupStateResponse,
  fetchReleasesByProvider,
  fetchReleasesResponse
} from '../../../actions/universe';
import {
  abortTask,
  abortTaskResponse,
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../actions/tasks';
import {
  fetchRunTimeConfigs,
  fetchRunTimeConfigsResponse,
  fetchProviderRunTimeConfigs,
  fetchProviderRunTimeConfigsResponse,
  getAlerts,
  getAlertsSuccess,
  getAlertsFailure
} from '../../../actions/customers';

import { openDialog, closeDialog } from '../../../actions/modal';

import {
  fetchUniverseTables,
  fetchUniverseTablesSuccess,
  fetchUniverseTablesFailure,
  resetTablesList
} from '../../../actions/tables';
import { isDefinedNotNull, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { toast } from 'react-toastify';
import { compareYBSoftwareVersions, getPrimaryCluster } from '../../../utils/universeUtilsTyped';
import { sortVersion } from '../../releases';

const mapDispatchToProps = (dispatch) => {
  return {
    getUniverseInfo: (uuid) => {
      return dispatch(fetchUniverseInfo(uuid)).then((response) => {
        return dispatch(fetchUniverseInfoResponse(response.payload));
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

    fetchSupportedReleases: (pUUID) => {
      dispatch(fetchReleasesByProvider(pUUID)).then((response) => {
        dispatch(fetchReleasesResponse(response.payload));
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
    showGFlagsNewModal: () => {
      dispatch(openDialog('gFlagsNewModal'));
    },
    showHelmOverridesModal: () => {
      dispatch(openDialog('helmOverridesModal'));
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
    showLinuxSoftwareUpgradeModal : () => {
      dispatch(openDialog('linuxVersionUpgradeModal'));
    },
    showSoftwareUpgradesNewModal: () => {
      dispatch(openDialog('softwareUpgradesNewModal'));
    },
    showRollbackModal: () => {
      dispatch(openDialog('rollbackModal'));
    },
    showVMImageUpgradeModal: () => {
      dispatch(openDialog('vmImageUpgradeModal'));
    },
    showRunSampleAppsModal: () => {
      dispatch(openDialog('runSampleAppsModal'));
    },
    showSupportBundleModal: () => {
      dispatch(openDialog('supportBundleModal'));
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
    showThirdpartyUpgradeModal: () => {
      dispatch(openDialog('thirdpartyUpgradeModal'));
    },
    showEnableYSQLModal: () => {
      dispatch(openDialog('enableYSQLModal'));
    },
    showEnableYCQLModal: () => {
      dispatch(openDialog('enableYCQLModal'));
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
    abortTask: (taskUUID) => {
      return dispatch(abortTask(taskUUID)).then((response) => {
        return dispatch(abortTaskResponse(response.payload));
      });
    },
    hideTaskAbortModal: () => {
      dispatch(closeDialog());
    },
    showTaskAbortModal: () => {
      dispatch(openDialog('confirmAbortTask'));
    },
    fetchRunTimeConfigs: (universeUUID) => {
      return dispatch(fetchRunTimeConfigs(universeUUID, true)).then((response) =>
        dispatch(fetchRunTimeConfigsResponse(response.payload))
      );
    },
    fetchProviderRunTimeConfigs: (providerUUID) => {
      return dispatch(fetchProviderRunTimeConfigs(providerUUID, true)).then((response) =>
        dispatch(fetchProviderRunTimeConfigsResponse(response.payload))
      );
    }
  };
};

function mapStateToProps(state) {
  const getAvailableSoftwareUpdateCount = (state) => {
    try {
      if (
        isDefinedNotNull(state.universe.currentUniverse.data) &&
        isNonEmptyObject(state.universe.currentUniverse.data)
      ) {
        const primaryCluster = getPrimaryCluster(
          state.universe.currentUniverse.data.universeDetails.clusters
        );
        const currentVersion = primaryCluster?.userIntent?.ybSoftwareVersion ?? null;
        if (currentVersion) {
          const supportedSoftwareVersions =
            state.universe.supportedReleases?.data?.toSorted(sortVersion) ?? [];
          const matchIndex = supportedSoftwareVersions.findIndex(
            (version) => compareYBSoftwareVersions(currentVersion, version) >= 0
          );
          return matchIndex === -1 ? 0 : matchIndex;
        }
      }
      return 0;
    } catch (err) {
      console.error('Versions comparison failed with: ' + err);
      return 0;
    }
  };

  return {
    customer: state.customer,
    universe: state.universe,
    tasks: state.tasks,
    universeTables: state.tables.universeTablesList,
    modal: state.modal,
    providers: state.cloud.providers,
    updateAvailable: getAvailableSoftwareUpdateCount(state),
    featureFlags: state.featureFlags,
    accessKeys: state.cloud.accessKeys,
    graph: state.graph
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDetail);
