// Copyright (c) YugaByte, Inc.

import { formValueSelector, reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import { RollingUpgradeForm } from '../../../common/forms';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../../actions/tasks';
import {
  rollingUpgrade,
  rollingUpgradeResponse,
  closeUniverseDialog,
  resetRollingUpgrade,
  fetchUniverseTasks,
  fetchUniverseTasksResponse,
  fetchUniverseMetadata,
  fetchUniverseInfo,
  fetchUniverseInfoResponse
} from '../../../../actions/universe';
import { isDefinedNotNull, isNonEmptyObject } from '../../../../utils/ObjectUtils';
import { getPrimaryCluster } from '../../../../utils/UniverseUtils';
import { TASK_LONG_TIMEOUT } from '../../../tasks/constants';
import { getPromiseState } from '../../../../utils/PromiseUtils';

const FORM_NAME = 'RollingUpgradeForm';

const mapDispatchToProps = (dispatch) => {
  return {
    /**
     * Dispatch Rolling Upgrade/ Gflag / TLS restart to endpoint and handle response.
     * @param values form data payload
     * @param universeUUID UUID of the current Universe
     */
    submitRollingUpgradeForm: (values, universeUUID) => {
      dispatch(resetRollingUpgrade()); // reset previous rollingUpgrade() error state, if any
      return dispatch(rollingUpgrade(values, universeUUID)).then((response) => {
        if (!response.error) {
          dispatch(closeUniverseDialog());
        }
        return dispatch(rollingUpgradeResponse(response.payload));
      });
    },
    fetchCustomerTasks: () => {
      dispatch(fetchCustomerTasks()).then((response) => {
        if (!response.error) {
          dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    },
    fetchUniverseTasks: (uuid) => {
      dispatch(fetchUniverseTasks(uuid)).then((response) => {
        dispatch(fetchUniverseTasksResponse(response.payload));
      });
    },
    resetRollingUpgrade: () => {
      dispatch(resetRollingUpgrade());
    },

    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    },

    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  const {
    universe: { currentUniverse },
    featureFlags: { test, released }
  } = state;

  const initialValues = {};
  let intialSystemdValue = false;
  if (isNonEmptyObject(currentUniverse) && isNonEmptyObject(currentUniverse.data.universeDetails)) {
    initialValues.tlsCertificate = currentUniverse.data.universeDetails.rootCA;

    const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
    intialSystemdValue = primaryCluster.userIntent.useSystemd;
    if (isDefinedNotNull(primaryCluster)) {
      initialValues.ybSoftwareVersion = primaryCluster.userIntent.ybSoftwareVersion;

      const masterGFlags = primaryCluster.userIntent.masterGFlags;
      const tserverGFlags = primaryCluster.userIntent.tserverGFlags;
      const gFlagArray = [];
      if (isNonEmptyObject(masterGFlags)) {
        Object.keys(masterGFlags).forEach((key) => {
          const masterObj = {};
          if (Object.prototype.hasOwnProperty.call(tserverGFlags, key)) {
            masterObj['TSERVER'] = tserverGFlags[key];
          }
          masterObj['Name'] = key;
          masterObj['MASTER'] = masterGFlags[key];
          gFlagArray.push(masterObj);
        });
      }
      if (isNonEmptyObject(tserverGFlags)) {
        Object.keys(tserverGFlags).forEach((key) => {
          const tserverObj = {};
          if (!Object.prototype.hasOwnProperty.call(masterGFlags, key)) {
            tserverObj['TSERVER'] = tserverGFlags[key];
            tserverObj['Name'] = key;
            gFlagArray.push(tserverObj);
          }
        });
      }
      initialValues.gFlags = gFlagArray;
    }
  }
  initialValues.timeDelay = TASK_LONG_TIMEOUT / 1000;
  initialValues.upgradeOption = 'Rolling';
  initialValues.rollingUpgrade = true;
  initialValues.systemdValue = intialSystemdValue;
  initialValues.universeOverrides = '';
  initialValues.azOverrides = '';
  let certificates = [];
  const allCertificates = state.customer.userCertificates;
  if (getPromiseState(allCertificates).isSuccess()) {
    certificates = [
      { label: 'Create New Certificate', uuid: null },
      ...(allCertificates.data || [])
    ];
  }

  const selector = formValueSelector(FORM_NAME);
  const formValues = selector(
    state,
    'upgradeOption',
    'systemdValue',
    'ybSoftwareVersion',
    'tlsCertificate',
    'gFlags'
  );

  return {
    modal: state.modal,
    universe: state.universe,
    softwareVersions: state.customer.softwareVersions,
    featureFlags: state.featureFlags,
    initialValues,
    certificates,
    formValues,
    enableNewEncryptionInTransitModal:
      test.enableNewEncryptionInTransitModal || released.enableNewEncryptionInTransitModal
  };
}

const rollingUpgradeForm = reduxForm({
  form: FORM_NAME,
  enableReinitialize: true, // to reinitialize form every time the initialValues prop changes
  keepDirtyOnReinitialize: true
});

export default connect(mapStateToProps, mapDispatchToProps)(rollingUpgradeForm(RollingUpgradeForm));
