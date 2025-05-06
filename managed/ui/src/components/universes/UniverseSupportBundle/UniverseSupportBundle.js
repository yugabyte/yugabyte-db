// Copyright (c) YugaByte, Inc.

import { Fragment, useCallback, useEffect, useState } from 'react';
import axios from 'axios';
import { connect, useDispatch, useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { useQueryClient } from 'react-query';

import { FirstStep } from './FirstStep/FirstStep';
import {
  DEFAULT_PROMETHEUS_METRICS_PARAMS,
  SecondStep,
  updateOptions
} from './SecondStep/SecondStep';
import { ThirdStep } from './ThirdStep/ThirdStep';
import { YBButton } from '../../common/forms/fields';
import { ROOT_URL } from '../../../config';
import { getSupportBundles } from '../../../selector/supportBundle';
import { isEmptyObject } from '../../../utils/ObjectUtils';
import {
  createSupportBundle,
  listSupportBundle,
  setListSupportBundle
} from '../../../actions/supportBundle';
import { filterTypes } from '../../metrics/MetricsComparisonModal/ComparisonFilterContextProvider';
import { getIsKubernetesUniverse } from '../../../utils/UniverseUtils';
import { getUniverseStatus } from '../helpers/universeHelpers';
import { RBAC_ERR_MSG_NO_PERM } from '../../../redesign/features/rbac/common/validator/ValidatorUtils';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { YBModal } from '../../../redesign/components';

import 'react-bootstrap-table/css/react-bootstrap-table.css';
import './UniverseSupportBundle.scss';

const stepsObj = {
  firstStep: 'firstStep',
  secondStep: 'secondStep',
  thirdStep: 'thirdStep'
};

const POLLING_INTERVAL = 10000; // ten seconds
export const UniverseSupportBundle = (props) => {
  const {
    currentUniverse: { universeDetails },
    button,
    closeModal,
    modal: { showModal, visibleModal }
  } = props;
  const [steps, setSteps] = useState(stepsObj.firstStep);
  const defaultOptions = updateOptions(
    filterTypes[0],
    [true, true, true, true, true, true, true, true, true, true, true],
    () => {},
    {},
    DEFAULT_PROMETHEUS_METRICS_PARAMS
  );
  const [payload, setPayload] = useState(defaultOptions);
  const isK8sUniverse = getIsKubernetesUniverse(props.currentUniverse);
  const dispatch = useDispatch();
  const [supportBundles] = useSelector(getSupportBundles);
  const queryClient = useQueryClient();

  const resetSteps = () => {
    if (supportBundles && Array.isArray(supportBundles) && supportBundles.length === 0) {
      setSteps(stepsObj.firstStep);
    } else {
      setSteps(stepsObj.thirdStep);
    }
  };

  const listSupportBundle = useCallback(
    (universeUUID) => {
      dispatch(getSupportBundle(universeUUID)).then((response) => {
        dispatch(setListSupportBundle(response.payload));
      });
    },
    [dispatch]
  );

  useEffect(() => {
    listSupportBundle(universeDetails.universeUUID);
  }, [listSupportBundle, universeDetails.universeUUID]);

  useEffect(() => {
    if (supportBundles && Array.isArray(supportBundles) && supportBundles.length === 0) {
      setSteps(stepsObj.firstStep);
    } else {
      if (steps !== stepsObj.secondStep) {
        setSteps(stepsObj.thirdStep);
      }
      if (
        supportBundles &&
        Array.isArray(supportBundles) &&
        supportBundles.find((supportBundle) => supportBundle.status === 'Running') !== undefined
      ) {
        setTimeout(() => {
          listSupportBundle(universeDetails.universeUUID);
        }, POLLING_INTERVAL);
      }
    }
  }, [supportBundles, listSupportBundle, universeDetails.universeUUID]);

  const saveSupportBundle = (universeUUID) => {
    dispatch(crateSupportBundle(universeUUID, payload)).then((response) => {
      if (response.error) {
        if (response?.payload?.response?.status === 403)
          toast.error(RBAC_ERR_MSG_NO_PERM, { autoClose: 3000 });
        else toast.error(createErrorMessage(response.payload));
      }
      handleStepChange(stepsObj.thirdStep);
      listSupportBundle(universeUUID);
      setPayload(defaultOptions);
    });
  };

  const handleStepChange = (step) => {
    setSteps(step);
  };

  const handleDeleteBundle = (universeUUID, bundleUUID) => {
    dispatch(deleteBundleByBundleUUID(universeUUID, bundleUUID)).then(() => {
      listSupportBundle(universeUUID);
    });
  };

  const handleDownloadBundle = (universeUUID, bundleUUID) => {
    downloadSupportBundle(universeUUID, bundleUUID);
  };

  const onClose = () => {
    queryClient.removeQueries('estimatedSupportBundleSize');
    resetSteps();
    closeModal();
  };

  const isSubmitDisabled = steps === stepsObj.secondStep && payload?.components?.length === 0;
  return (
    <Fragment>
      {isEmptyObject(button) ? (
        <YBButton btnText={'Support Bundle'} btnClass={'btn btn-default open-modal-btn'} />
      ) : (
        button
      )}
      <YBModal
        className="universe-support-bundle"
        title="Support Bundle"
        open={showModal && visibleModal === 'supportBundleModal'}
        onClose={onClose}
        overrideHeight="fit-content"
        cancelLabel="Close"
        submitLabel={steps === stepsObj.secondStep ? 'Create Bundle' : undefined}
        onSubmit={
          steps === stepsObj.secondStep
            ? () => {
                saveSupportBundle(universeDetails.universeUUID);
              }
            : undefined
        }
        buttonProps={{ primary: { disabled: isSubmitDisabled } }}
      >
        <div className="universe-support-bundle-body">
          {steps === stepsObj.firstStep && (
            <FirstStep
              onCreateSupportBundle={() => {
                handleStepChange(stepsObj.secondStep);
              }}
              universeUUID={universeDetails.universeUUID}
            />
          )}
          {steps === stepsObj.secondStep && (
            <SecondStep
              onOptionsChange={(selectedOptions) => {
                if (selectedOptions) {
                  setPayload(selectedOptions);
                } else {
                  setPayload(defaultOptions);
                }
              }}
              payload={payload}
              universeUUID={universeDetails.universeUUID}
              isK8sUniverse={isK8sUniverse}
              universeStatus={getUniverseStatus(props.currentUniverse)}
            />
          )}
          {steps === stepsObj.thirdStep && (
            <ThirdStep
              handleDownloadBundle={(bundleUUID) =>
                handleDownloadBundle(universeDetails.universeUUID, bundleUUID)
              }
              handleDeleteBundle={(bundleUUID) =>
                handleDeleteBundle(universeDetails.universeUUID, bundleUUID)
              }
              supportBundles={supportBundles}
              onCreateSupportBundle={() => {
                handleStepChange(stepsObj.secondStep);
              }}
              universeUUID={universeDetails.universeUUID}
            />
          )}
        </div>
      </YBModal>
    </Fragment>
  );
};

export function getSupportBundle(universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle`;
  const request = axios.get(endpoint);
  return listSupportBundle(request);
}

export function crateSupportBundle(universeUUID, supportBundle) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle`;
  const request = axios.post(endpoint, supportBundle);
  return createSupportBundle(request);
}

export function deleteBundleByBundleUUID(universeUUID, supportBundleUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle/${supportBundleUUID}`;
  const request = axios.delete(endpoint);
  return createSupportBundle(request);
}

export function downloadSupportBundle(universeUUID, supportBundleUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle/${supportBundleUUID}/download`;
  window.open(endpoint, '_blank');
}

export function fetchEstimatedSupportBundleSize(universeUUID, supportBundle) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle/estimate_size`;
  return axios.post(endpoint, supportBundle).then((response) => response.data);
}

function mapStateToProps(state) {
  return {
    supportBundle: state.supportBundle
  };
}

export default connect(mapStateToProps)(UniverseSupportBundle);
